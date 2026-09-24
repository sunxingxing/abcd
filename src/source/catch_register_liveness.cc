#include "catch_register_liveness.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/register_access.h"

namespace abcd::source {
namespace {
constexpr std::size_t kLivenessPlanes = 3;
BodyEmitFailure Fail(BodyEmitError kind, abc::CodeOffset offset = {}) {
  return {kind, offset};
}
void Mark(CatchRegisterLiveness::Bits& bits, std::uint32_t reg) {
  bits.words[reg / kCatchWordBits] |=
      std::uint64_t{1} << (reg % kCatchWordBits);
}
bool Join(CatchRegisterLiveness::Bits& target,
          const CatchRegisterLiveness::Bits& source,
          const CatchRegisterLiveness::Bits* kill) {
  bool changed = false;
  for (std::size_t i = 0; i < target.words.size(); ++i) {
    const auto next = source.words[i] &
                      (kill ? ~kill->words[i] : UINT64_MAX);
    changed |= (target.words[i] | next) != target.words[i];
    target.words[i] |= next;
  }
  return changed;
}
}  // namespace

bool CatchRegisterLiveness::Bits::Has(abc::RegisterId reg) const {
  return reg.value < kCatchTrackedRegisters &&
         (words[reg.value / kCatchWordBits] &
          (std::uint64_t{1} << (reg.value % kCatchWordBits))) != 0;
}

std::size_t CatchRegisterLiveness::AllocatedBytes() const {
  return successors_.capacity() * sizeof(Successors) +
         predecessor_starts_.capacity() * sizeof(std::size_t) +
         predecessors_.capacity() * sizeof(Edge) +
         (uses_.capacity() + definitions_.capacity() +
          live_before_.capacity()) * sizeof(Bits);
}

bool CatchRegisterLiveness::DeadAfter(std::size_t call,
                                      abc::RegisterId reg) const {
  return call < live_before_.size() && reg.value < kCatchTrackedRegisters &&
         !live_before_[call].Has(reg);
}

std::variant<CatchRegisterLiveness, BodyEmitFailure>
CatchRegisterLiveness::Build(const abc::MethodBody& body,
                             std::size_t try_begin, std::size_t try_end,
                             std::size_t handler_begin,
                             std::uint64_t frame_size, std::size_t bytes,
                             std::uint64_t work) {
  using abc::ControlTransfer;
  const auto& sites = body.instructions;
  const auto count = sites.size();
  if (count == 0 || count > UINT32_MAX ||
      try_begin >= try_end || try_end > count || handler_begin >= count)
    return Fail(BodyEmitError::kExceptionRegions);
  constexpr std::size_t kFixedBytes = sizeof(Successors) +
                                      sizeof(std::size_t) +
                                      kLivenessPlanes * sizeof(Bits);
  if (count > (bytes - std::min(bytes, sizeof(std::size_t))) / kFixedBytes)
    return Fail(BodyEmitError::kResourceLimit);
  try {
    CatchRegisterLiveness result;
    result.successors_.resize(count);
    result.predecessor_starts_.resize(count + 1);
    result.uses_.resize(count);
    result.definitions_.resize(count);
    result.live_before_.resize(count);
    if (result.AllocatedBytes() > bytes)
      return Fail(BodyEmitError::kResourceLimit);
    std::uint64_t used_work = 0;
    const auto charge = [&](std::uint64_t units = 1) {
      if (units > work - used_work) return false;
      used_work += units;
      return true;
    };
    const auto target = [&](abc::CodeOffset pc) -> std::optional<std::uint32_t> {
      const auto found = std::lower_bound(
          sites.begin(), sites.end(), pc,
          [](const abc::BodyInstruction& site, abc::CodeOffset offset) {
            return site.offset < offset;
          });
      if (found == sites.end() || found->offset != pc) return {};
      return static_cast<std::uint32_t>(found - sites.begin());
    };
    std::size_t edges = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if (!charge()) return Fail(BodyEmitError::kWorkLimit);
      auto& next = result.successors_[i];
      const auto transfer = abc::GetControlTransfer(sites[i].decoded.opcode);
      if (!transfer || *transfer == ControlTransfer::kSuspend)
        return Fail(BodyEmitError::kUnsupportedInstruction, sites[i].offset);
      const auto append = [&](std::uint32_t destination, bool exceptional) {
        next.edges[next.count++] = {destination, exceptional};
        ++result.predecessor_starts_[destination + 1];
        ++edges;
      };
      if (*transfer == ControlTransfer::kJump ||
          *transfer == ControlTransfer::kBranch) {
        if (!sites[i].branch_target)
          return Fail(BodyEmitError::kUnsupportedInstruction, sites[i].offset);
        const auto jump = target(*sites[i].branch_target);
        if (!jump) return Fail(BodyEmitError::kUnsupportedInstruction,
                               sites[i].offset);
        append(*jump, false);
      }
      if (*transfer == ControlTransfer::kFallthrough ||
          *transfer == ControlTransfer::kBranch) {
        if (i + 1 >= count)
          return Fail(BodyEmitError::kUnsupportedInstruction, sites[i].offset);
        append(static_cast<std::uint32_t>(i + 1), false);
      }
      if (i >= try_begin && i < try_end)
        append(static_cast<std::uint32_t>(handler_begin), true);
      const auto accesses =
          abc::GetRegisterAccesses(sites[i].decoded, frame_size);
      const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
      if (!known)
        return Fail(BodyEmitError::kUnsupportedInstruction, sites[i].offset);
      for (std::size_t window = 0; window < known->count; ++window) {
        const auto& access = known->windows[window];
        const auto end = std::min<std::uint64_t>(
            std::uint64_t(access.first.value) + access.count,
            kCatchTrackedRegisters);
        for (std::uint32_t reg = access.first.value; reg < end; ++reg) {
          if (!charge()) return Fail(BodyEmitError::kWorkLimit);
          if (access.access != abc::OperandAccess::kWrite)
            Mark(result.uses_[i], reg);
          if (access.access != abc::OperandAccess::kRead)
            Mark(result.definitions_[i], reg);
        }
      }
    }
    if (edges > (bytes - result.AllocatedBytes()) / sizeof(Edge))
      return Fail(BodyEmitError::kResourceLimit);
    result.predecessors_.resize(edges);
    if (result.AllocatedBytes() > bytes)
      return Fail(BodyEmitError::kResourceLimit);
    for (std::size_t i = 1; i < result.predecessor_starts_.size(); ++i)
      result.predecessor_starts_[i] += result.predecessor_starts_[i - 1];
    const auto cursor_bytes = result.predecessor_starts_.size() *
                              sizeof(std::size_t);
    if (cursor_bytes > bytes - result.AllocatedBytes())
      return Fail(BodyEmitError::kResourceLimit);
    auto cursor = result.predecessor_starts_;
    if (cursor.capacity() * sizeof(std::size_t) >
        bytes - result.AllocatedBytes())
      return Fail(BodyEmitError::kResourceLimit);
    for (std::size_t i = 0; i < count; ++i)
      for (std::size_t edge = 0; edge < result.successors_[i].count; ++edge) {
        const auto next = result.successors_[i].edges[edge];
        result.predecessors_[cursor[next.index]++] =
            {static_cast<std::uint32_t>(i), next.exceptional};
      }
    const auto remaining = bytes - result.AllocatedBytes() -
                           cursor.capacity() * sizeof(std::size_t);
    if (count > remaining / (sizeof(std::uint32_t) + sizeof(std::uint8_t)))
      return Fail(BodyEmitError::kResourceLimit);
    std::vector<std::uint32_t> queue(count);
    std::vector<std::uint8_t> queued(count, 1);
    const auto scratch = queue.capacity() * sizeof(std::uint32_t) +
                         queued.capacity();
    if (result.AllocatedBytes() + scratch +
            cursor.capacity() * sizeof(std::size_t) > bytes)
      return Fail(BodyEmitError::kResourceLimit);
    std::size_t head = 0, tail = 0, waiting = count;
    for (std::size_t i = count; i-- > 0;)
      queue[tail++] = static_cast<std::uint32_t>(i);
    if (tail == count) tail = 0;
    while (waiting != 0) {
      if (!charge()) return Fail(BodyEmitError::kWorkLimit);
      const auto id = queue[head];
      head = (head + 1) % count;
      --waiting;
      queued[id] = 0;
      auto next = result.uses_[id];
      for (std::size_t i = 0; i < result.successors_[id].count; ++i) {
        if (!charge()) return Fail(BodyEmitError::kWorkLimit);
        const auto edge = result.successors_[id].edges[i];
        Join(next, result.live_before_[edge.index],
             edge.exceptional ? nullptr : &result.definitions_[id]);
      }
      if (!Join(result.live_before_[id], next, nullptr)) continue;
      for (std::size_t i = result.predecessor_starts_[id];
           i < result.predecessor_starts_[id + 1]; ++i) {
        if (!charge()) return Fail(BodyEmitError::kWorkLimit);
        const auto predecessor = result.predecessors_[i].index;
        if (queued[predecessor]) continue;
        queue[tail] = predecessor;
        tail = (tail + 1) % count;
        ++waiting;
        queued[predecessor] = 1;
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit);
  }
}
}  // namespace abcd::source
