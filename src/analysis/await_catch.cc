#include "abcd/analysis/await_catch.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>

#include "abcd/analysis/exception_dispatch.h"

namespace abcd::analysis {
namespace {
enum class FirstUse : std::uint8_t { kUnknown, kRead, kWrite, kExit, kSplit };
struct WorkLimit {};
void Charge(std::uint64_t& remaining, std::uint64_t units = 1) {
  if (units > remaining) throw WorkLimit{};
  remaining -= units;
}
struct CatchProbe {
  abc::CodeOffset handler;
  abc::RegisterId slot;
  abc::CodeOffset await;
};
constexpr std::size_t kResumeRegisters = 2;
// Standard lazy binary segment tree uses fewer than 4*N nodes for N leaves.
constexpr std::size_t kTreeNodesPerSlot = 4;
class FirstUseIndex {
 public:
  FirstUseIndex(std::span<FirstUse> nodes, std::size_t slots, std::uint64_t& work)
      : nodes_(nodes), slots_(slots), work_(work) {}
  void Reset(FirstUse use) { nodes_[1] = use; }
  void Assign(std::size_t begin, std::size_t end, FirstUse use) {
    if (begin != end) Assign(1, 0, slots_, begin, end, use);
  }
  FirstUse At(std::size_t slot) {
    std::size_t node = 1, begin = 0, end = slots_;
    while (true) {
      Charge(work_);
      if (nodes_[node] != FirstUse::kSplit) return nodes_[node];
      const auto middle = begin + (end - begin) / 2;
      node *= 2;
      if (slot < middle) end = middle;
      else { ++node; begin = middle; }
    }
  }

 private:
  void Assign(std::size_t node, std::size_t begin, std::size_t end,
              std::size_t first, std::size_t last, FirstUse use) {
    Charge(work_);
    if (first <= begin && end <= last) { nodes_[node] = use; return; }
    if (nodes_[node] != FirstUse::kSplit) {
      nodes_[node * 2] = nodes_[node];
      nodes_[node * 2 + 1] = nodes_[node];
      nodes_[node] = FirstUse::kSplit;
    }
    const auto middle = begin + (end - begin) / 2;
    if (first < middle) Assign(node * 2, begin, middle, first, last, use);
    if (last > middle) Assign(node * 2 + 1, middle, end, first, last, use);
  }
  std::span<FirstUse> nodes_;
  std::size_t slots_;
  std::uint64_t& work_;
};
}  // namespace

std::variant<AwaitCatchProof, AwaitCatchFailure> ProveAwaitCatchInputs(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame_size,
    abc::CodeOffset completion_handler, SemanticBudget budget) {
  const auto fail = [](AwaitCatchError kind, abc::CodeOffset pc = {}) {
    return AwaitCatchFailure{kind, pc};
  };
  if (regions.empty()) return AwaitCatchProof{};
  try {
    auto work = budget.work;
    const auto charge = [&] { Charge(work); };
    const auto selected = BuildExceptionDispatch(body, profile, {budget.bytes});
    const auto* dispatch = std::get_if<ExceptionDispatch>(&selected);
    if (!dispatch) {
      const auto& error = std::get<DispatchFailure>(selected);
      return fail(error.kind == DispatchError::kResourceLimit
          ? AwaitCatchError::kResourceLimit : AwaitCatchError::kRegion, error.offset);
    }
    const auto retain = [&](std::size_t bytes) {
      if (bytes > budget.bytes) throw std::bad_alloc{};
      budget.bytes -= bytes;
    };
    retain(dispatch->spans.capacity() * sizeof(DispatchSpan));
    if (regions.size() > budget.bytes / kResumeRegisters / sizeof(CatchProbe))
      throw std::bad_alloc{};
    std::vector<CatchProbe> probes;
    probes.reserve(regions.size() * kResumeRegisters);
    retain(probes.capacity() * sizeof(CatchProbe));
    const auto target_at = [&](abc::CodeOffset pc) {
      Charge(work, 1 + std::bit_width(dispatch->spans.size()));
      return dispatch->Find(pc);
    };
    const auto local = [&](abc::CodeOffset pc) {
      const auto target = target_at(pc);
      return target && target->handler_pc != completion_handler;
    };
    for (const auto& region : regions) {
      charge();
      const auto target = target_at(region.await);
      if (target != target_at(region.reject) ||
          region.result.value >= frame_size || region.resume_mode.value >= frame_size)
        return fail(AwaitCatchError::kRegion, region.await);
      if (!target || target->handler_pc == completion_handler) continue;
      probes.push_back({target->handler_pc, region.result, region.await});
      probes.push_back({target->handler_pc, region.resume_mode, region.await});
    }
    if (probes.empty()) return AwaitCatchProof{budget.work - work};
    std::sort(probes.begin(), probes.end(), [&](const auto& a, const auto& b) {
      charge();
      return a.handler > b.handler;
    });
    if (probes.size() > budget.bytes / sizeof(abc::RegisterId)) throw std::bad_alloc{};
    std::vector<abc::RegisterId> slots;
    slots.reserve(probes.size());
    retain(slots.capacity() * sizeof(abc::RegisterId));
    for (const auto& probe : probes) { charge(); slots.push_back(probe.slot); }
    std::sort(slots.begin(), slots.end(), [&](auto a, auto b) { charge(); return a < b; });
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    if (slots.size() > budget.bytes / kTreeNodesPerSlot / sizeof(FirstUse))
      throw std::bad_alloc{};
    std::vector<FirstUse> nodes(slots.size() * kTreeNodesPerSlot, FirstUse::kUnknown);
    retain(nodes.capacity() * sizeof(FirstUse));
    FirstUseIndex uses{nodes, slots.size(), work};
    const auto position = [&](std::uint64_t reg) {
      return static_cast<std::size_t>(std::lower_bound(slots.begin(), slots.end(), reg,
          [&](abc::RegisterId slot, std::uint64_t value) {
            charge(); return slot.value < value;
          }) - slots.begin());
    };
    const auto mark = [&](const ScalarSlot& slot, FirstUse use) {
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
        uses.Assign(position(reg->value), position(std::uint64_t(reg->value) + 1), use);
    };
    std::size_t next_probe = 0;
    for (auto site = body.instructions.rbegin(); site != body.instructions.rend(); ++site) {
      charge();
      const auto lifted = LiftSemanticInstruction(*site, profile, frame_size);
      const auto* instruction = std::get_if<SemanticInstruction>(&lifted);
      if (!instruction) {
        uses.Reset(FirstUse::kUnknown);
      } else {
        const bool throws_locally = instruction->effects.exceptional !=
            ExceptionalCompletion::kImpossible && local(site->offset);
        const bool branch = std::holds_alternative<BranchOperation>(instruction->operation) ||
                            std::holds_alternative<JumpOperation>(instruction->operation);
        const bool exits = std::holds_alternative<ReturnOperation>(instruction->operation) ||
            (std::holds_alternative<ThrowOperation>(instruction->operation) && !throws_locally);
        if (throws_locally || branch) uses.Reset(FirstUse::kUnknown);
        else if (exits) uses.Reset(FirstUse::kExit);
        const auto& registers = instruction->effects.normal_registers;
        // A normal-only write cannot kill the incoming value on a local
        // exception edge. Reads precede writes and therefore win below.
        if (registers.write && !throws_locally && !branch)
          mark(*registers.write, FirstUse::kWrite);
        for (std::size_t i = 0; i < registers.read_count; ++i)
          mark(registers.reads[i], FirstUse::kRead);
        if (registers.range)
          uses.Assign(position(registers.range->first.value),
              position(std::uint64_t(registers.range->first.value) + registers.range->count),
              FirstUse::kRead);
      }
      if (next_probe < probes.size() && probes[next_probe].handler > site->offset)
        return fail(AwaitCatchError::kRegion, probes[next_probe].await);
      while (next_probe < probes.size() && probes[next_probe].handler == site->offset) {
        const auto& probe = probes[next_probe++];
        const auto use = uses.At(position(probe.slot.value));
        if (use != FirstUse::kWrite && use != FirstUse::kExit)
          return fail(AwaitCatchError::kObservedRegister, probe.await);
      }
      if (next_probe == probes.size()) return AwaitCatchProof{budget.work - work};
    }
    return fail(AwaitCatchError::kRegion);
  } catch (const WorkLimit&) { return fail(AwaitCatchError::kResourceLimit);
  } catch (const std::bad_alloc&) { return fail(AwaitCatchError::kResourceLimit);
  } catch (const std::length_error&) { return fail(AwaitCatchError::kResourceLimit); }
}
}  // namespace abcd::analysis
