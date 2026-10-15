#include "concurrent_async.h"

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/register_effect.h"

namespace abcd::source {
namespace {
using namespace analysis;
// No dense register state: backwards copy substitution tracks exactly the two
// observed values (notification input and return input). Each suffix node is
// visited once; branches must agree on the same physical completion port.
struct Slot {
  std::optional<abc::RegisterId> reg;
  bool operator==(const Slot&) const = default;
};
Slot Of(const ScalarSlot& slot) { return {std::holds_alternative<abc::RegisterId>(slot) ?
    std::optional{std::get<abc::RegisterId>(slot)} : std::nullopt}; }
struct Suffix {
  std::optional<InstructionId> returned, notification;
  Slot result, notified;
  bool operator==(const Suffix&) const = default;
};
class Proof {
 public:
  Proof(const abc::MethodBody& body, const abc::FrameLayout& frame,
        abc::ProfileId profile, SemanticBudget budget)
      : body_(body), frame_(frame), profile_(profile), initial_(budget), left_(budget) {}
  AsyncNotifications Run() {
    const auto n = body_.instructions.size();
    Require(n && n <= UINT32_MAX, ConcurrentError::kCorrespondence);
    auto built = BuildNormalFlow(body_, {left_.bytes});
    if (const auto* error = std::get_if<FlowFailure>(&built))
      Fail(error->kind == FlowError::kResourceLimit ? ConcurrentError::kResourceLimit : ConcurrentError::kCorrespondence);
    auto flow = std::get<NormalFlow>(std::move(built));
    Admit(flow.blocks.capacity(), sizeof(FlowBlock)); Step(n + flow.blocks.size());
    Array(next_, n); Array(count_, n); Array(incoming_, n + 1); Array(suffix_, n);
    Require(n <= SIZE_MAX / 2, ConcurrentError::kResourceLimit); Array(queue_, 2 * n); Array(result_.members, n); Array(degree_, n);
    for (const auto& block : flow.blocks) {
      for (std::uint32_t i = block.first_instruction; i < block.first_instruction + block.instruction_count; ++i) {
        Step();
        if (i + 1 < block.first_instruction + block.instruction_count) next_[i][count_[i]++] = i + 1;
        else for (unsigned e = 0; e < block.successor_count; ++e) {
          Require(block.successors[e].target.value < flow.blocks.size(), ConcurrentError::kCorrespondence);
          next_[i][count_[i]++] = flow.blocks[block.successors[e].target.value].first_instruction;
        }
        for (unsigned e = 0; e < count_[i]; ++e) ++incoming_[next_[i][e] + 1];
      }
    }
    for (std::size_t i = 1; i < incoming_.size(); ++i) { Step(); incoming_[i] += incoming_[i - 1]; }
    Array(predecessors_, incoming_.back());
    std::vector<std::size_t> positions;
    Array(positions, incoming_.size()); std::copy(incoming_.begin(), incoming_.end(), positions.begin());
    for (std::uint32_t i = 0; i < n; ++i) {
      degree_[i] = count_[i];
      for (unsigned e = 0; e < count_[i]; ++e) { Step(); predecessors_[positions[next_[i][e]]++] = i; }
    }
    std::size_t head = 0, tail = 0;
    for (std::uint32_t i = 0; i < n; ++i) if (!degree_[i]) queue_[tail++] = i;
    while (head < tail) {
      const auto i = queue_[head++]; current_ = i; Step();
      const auto lifted = LiftSemanticInstruction(body_.instructions[i], profile_, frame_.registers, &frame_);
      const auto* op = std::get_if<SemanticInstruction>(&lifted);
      if (op && std::holds_alternative<ReturnOperation>(op->operation)) {
        const auto& value = std::get<ReturnOperation>(op->operation).value;
        if (std::holds_alternative<Accumulator>(value)) suffix_[i].returned = InstructionId{i};
      } else if (op && !std::holds_alternative<AsyncSettle>(op->operation) && count_[i]) {
        auto s = suffix_[next_[i][0]];
        for (unsigned e = 1; e < count_[i]; ++e) if (s != suffix_[next_[i][e]]) s = {};
        if (s.returned) {
          if (const auto* notification = std::get_if<ConcurrentNotification>(&op->operation)) {
            if (!s.notification && notification->owner == NotificationOwner::kInvocationFunction &&
                op->effects.exceptional == ExceptionalCompletion::kImpossible) {
              s.notification = InstructionId{i}; s.notified = {}; suffix_[i] = s;
            }
          } else if (op->effects.normal == LanguageEffect::kNone && op->effects.before_throw == LanguageEffect::kNone &&
                     op->effects.exceptional == ExceptionalCompletion::kImpossible &&
                     abc::GetControlTransfer(body_.instructions[i].decoded.opcode) != abc::ControlTransfer::kSuspend) {
            bool valid = true;
            const auto registers = LiftRegisterEffect(body_.instructions[i].decoded, frame_.registers);
            const auto* effect = std::get_if<RegisterEffect>(&registers);
            if (!effect) valid = false;
            else if (effect->write) {
              const auto dst = Of(*effect->write);
              const auto* assignment = std::get_if<ScalarAssignment>(&op->operation);
              const auto* value = assignment ? std::get_if<ScalarValue>(&assignment->expression) : nullptr;
              const auto replace = [&](Slot& slot) {
                if (slot != dst) return;
                if (!value) { valid = false; return; }
                if (const auto* reg = std::get_if<abc::RegisterId>(value)) slot.reg = *reg;
                else if (std::holds_alternative<Accumulator>(*value)) slot.reg.reset();
                else valid = false;
              };
              replace(s.result); if (s.notification) replace(s.notified);
            }
            if (valid) suffix_[i] = s;
          }
        }
      }
      for (auto p = incoming_[i]; p < incoming_[i + 1]; ++p) {
        Step(); auto predecessor = predecessors_[p];
        if (--degree_[predecessor] == 0) queue_[tail++] = predecessor;
      }
    }
    // Settlement starts a fresh promise-valued suffix; it never propagates its
    // result across a throwing event. An exception instead enters the existing
    // implicit reject handler, separately proved by async normalization.
    std::size_t ports = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
      Step(); const auto lifted = LiftSemanticInstruction(body_.instructions[i], profile_, frame_.registers, &frame_);
      const auto* op = std::get_if<SemanticInstruction>(&lifted);
      ports += op && std::holds_alternative<AsyncSettle>(op->operation);
    }
    Admit(ports, sizeof(AsyncNotificationPort)); result_.ports.reserve(ports);
    Admit(result_.ports.capacity() - ports, sizeof(AsyncNotificationPort));
    for (std::uint32_t i = 0; i < n; ++i) {
      current_ = i; Step();
      const auto lifted = LiftSemanticInstruction(body_.instructions[i], profile_, frame_.registers, &frame_);
      const auto* op = std::get_if<SemanticInstruction>(&lifted);
      const auto* settle = op ? std::get_if<AsyncSettle>(&op->operation) : nullptr;
      if (!settle) continue;
      Require(count_[i] == 1, ConcurrentError::kCompletion);
      const auto& s = suffix_[next_[i][0]];
      Require(s.returned.has_value(), ConcurrentError::kCompletion);
      Require(s.notification.has_value(), ConcurrentError::kMissingNotification);
      Require(!s.result.reg && !s.notified.reg, ConcurrentError::kNotificationValue);
      const auto port = static_cast<std::uint32_t>(result_.ports.size());
      result_.ports.push_back({{i}, *s.notification, *s.returned, settle->kind});
      head = tail = 0; queue_[tail++] = next_[i][0];
      while (head < tail) {
        const auto member = queue_[head++]; current_ = member; Step();
        if (result_.members[member]) { Require(*result_.members[member] == port, ConcurrentError::kCompletion); continue; }
        result_.members[member] = port;
        if (member == s.returned->value) continue;
        Require(suffix_[member].returned == s.returned && (!suffix_[member].notification || suffix_[member].notification == s.notification),
                ConcurrentError::kCorrespondence);
        for (unsigned e = 0; e < count_[member]; ++e) {
          Require(tail < queue_.size(), ConcurrentError::kResourceLimit); queue_[tail++] = next_[member][e];
        }
      }
    }
    Require(!result_.ports.empty(), ConcurrentError::kAsync);
    for (std::uint32_t i = 0; i < n; ++i) {
      current_ = i; Step();
      const auto lifted = LiftSemanticInstruction(body_.instructions[i], profile_, frame_.registers, &frame_);
      const auto* op = std::get_if<SemanticInstruction>(&lifted);
      if (op && (std::holds_alternative<ReturnOperation>(op->operation) || std::holds_alternative<ConcurrentNotification>(op->operation)))
        Require(result_.members[i].has_value(), ConcurrentError::kCompletion);
      if (!result_.members[i]) continue;
      const auto port = *result_.members[i];
      for (auto p = incoming_[i]; p < incoming_[i + 1]; ++p) {
        Step(); const auto predecessor = predecessors_[p];
        Require(predecessor == result_.ports[port].settlement.value || result_.members[predecessor] == port,
                ConcurrentError::kEarlyNotification);
      }
    }
    for (const auto& handler : body_.handlers) {
      Step(); const auto i = std::lower_bound(body_.instructions.begin(), body_.instructions.end(), handler.offset,
          [](const auto& instruction, auto pc) { return instruction.offset < pc; });
      Require(i != body_.instructions.end() && i->offset == handler.offset && !result_.members[i - body_.instructions.begin()],
              ConcurrentError::kEarlyNotification);
    }
    result_.allocated_bytes = initial_.bytes - left_.bytes; result_.work = initial_.work - left_.work;
    return std::move(result_);
  }
 private:
  [[noreturn]] void Fail(ConcurrentError kind) const {
    ConcurrentFailure error; error.kind = kind;
    if (current_ && kind != ConcurrentError::kResourceLimit && kind != ConcurrentError::kWorkLimit)
      error.pc = body_.instructions[*current_].offset;
    throw error;
  }
  void Require(bool ok, ConcurrentError error) const { if (!ok) Fail(error); }
  void Admit(std::size_t n, std::size_t width) { Require(n <= left_.bytes / width, ConcurrentError::kResourceLimit); left_.bytes -= n * width; }
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, ConcurrentError::kWorkLimit); left_.work -= n; }
  template<class T> void Array(std::vector<T>& v, std::size_t n) { Admit(n, sizeof(T)); v.resize(n); Admit(v.capacity() - n, sizeof(T)); }
  const abc::MethodBody& body_; const abc::FrameLayout& frame_; abc::ProfileId profile_; SemanticBudget initial_, left_;
  std::optional<std::uint32_t> current_;
  AsyncNotifications result_; std::vector<std::array<std::uint32_t, 2>> next_;
  std::vector<std::uint8_t> count_, degree_; std::vector<std::size_t> incoming_;
  std::vector<std::uint32_t> predecessors_, queue_; std::vector<Suffix> suffix_;
};
}  // namespace
std::variant<AsyncNotifications, ConcurrentFailure> ProveAsyncNotifications(
    const abc::MethodBody& body, const abc::FrameLayout& frame, abc::ProfileId profile, analysis::SemanticBudget budget) {
  try { return Proof(body, frame, profile, budget).Run(); }
  catch (const ConcurrentFailure& error) { return error; }
  catch (const std::bad_alloc&) { ConcurrentFailure e; e.kind = ConcurrentError::kResourceLimit; return e; }
  catch (const std::length_error&) { ConcurrentFailure e; e.kind = ConcurrentError::kResourceLimit; return e; }
}
std::variant<std::uint64_t, ConcurrentFailure> VerifyAsyncNotifications(
    const abc::MethodBody& body, const abc::FrameLayout& frame, abc::ProfileId profile,
    const AsyncNotifications& proof, analysis::SemanticBudget budget) {
  auto result = ProveAsyncNotifications(body, frame, profile, budget);
  if (const auto* error = std::get_if<ConcurrentFailure>(&result)) return *error;
  const auto& actual = std::get<AsyncNotifications>(result);
  if (actual.ports != proof.ports || actual.members != proof.members) {
    ConcurrentFailure e; e.kind = ConcurrentError::kCorrespondence; return e;
  }
  return actual.work;
}
}  // namespace abcd::source

namespace abcd::source {
ConcurrentAsyncResult NormalizeConcurrentAsync(const abc::MethodBody& original,
    const abc::FrameLayout& frame, abc::ProfileId profile, analysis::SemanticBudget budget) {
  try {
    const auto initial = budget;
    const auto fail = [&](ConcurrentError kind, std::optional<abc::CodeOffset> pc = {}) {
      ConcurrentFailure error; error.kind = kind; error.pc = pc; throw error;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      if (n > budget.bytes / width) fail(ConcurrentError::kResourceLimit); budget.bytes -= n * width;
    };
    const auto step = [&](std::uint64_t n) {
      if (n > budget.work) fail(ConcurrentError::kWorkLimit); budget.work -= n;
    };
    const auto reserve = [&]<class T>(std::vector<T>& array, std::size_t n) {
      admit(n, sizeof(T)); array.reserve(n); admit(array.capacity() - n, sizeof(T));
    };
    auto built = ProveAsyncNotifications(original, frame, profile, budget);
    if (const auto* error = std::get_if<ConcurrentFailure>(&built)) return *error;
    const auto& proof = std::get<AsyncNotifications>(built); admit(proof.allocated_bytes, 1); step(proof.work);
    abc::MethodBody projected;
    std::vector<abc::CodeOffset> raw_pcs;
    reserve(projected.instructions, original.instructions.size()); reserve(raw_pcs, original.instructions.size());
    std::uint32_t pc = 0;
    for (std::size_t i = 0; i < original.instructions.size(); ++i) {
      step(1); const auto member = proof.members[i];
      if (member && proof.ports[*member].returned.value != i) continue;
      raw_pcs.push_back(original.instructions[i].offset);
      auto instruction = original.instructions[i]; instruction.offset = {pc};
      if (instruction.decoded.size > UINT32_MAX - pc) fail(ConcurrentError::kCorrespondence);
      pc += instruction.decoded.size; projected.instructions.push_back(instruction);
    }
    const auto old_end = original.instructions.back().offset.value + original.instructions.back().decoded.size;
    const auto map = [&](std::uint64_t old) -> abc::CodeOffset {
      if (old == old_end) return {pc};
      const auto found = std::lower_bound(raw_pcs.begin(), raw_pcs.end(), old,
          [](auto value, auto target) { return value.value < target; });
      if (found == raw_pcs.end()) fail(ConcurrentError::kCorrespondence);
      return projected.instructions[found - raw_pcs.begin()].offset;
    };
    for (auto& instruction : projected.instructions) {
      step(1); if (!instruction.branch_target) continue;
      const auto target = map(instruction.branch_target->value);
      const auto delta = std::int64_t(target.value) - instruction.offset.value;
      const auto width = instruction.decoded.operands[0].width_bits;
      if (!width || width > 32 || delta < -(std::int64_t{1} << (width - 1)) ||
          delta >= (std::int64_t{1} << (width - 1))) fail(ConcurrentError::kCorrespondence);
      instruction.branch_target = target; instruction.decoded.branch_delta = delta;
      instruction.decoded.operands[0].raw_bits = std::uint64_t(delta) & ((std::uint64_t{1} << width) - 1);
    }
    reserve(projected.tries, original.tries.size()); reserve(projected.handlers, original.handlers.size());
    for (auto region : original.tries) {
      step(1); const auto begin = map(region.offset.value), end = map(std::uint64_t(region.offset.value) + region.length);
      region.offset = begin; region.length = end.value - begin.value; projected.tries.push_back(region);
    }
    for (auto handler : original.handlers) {
      step(1); const auto begin = map(handler.offset.value), end = map(std::uint64_t(handler.offset.value) + handler.length);
      handler.offset = begin; handler.length = end.value - begin.value; projected.handlers.push_back(handler);
    }
    ConcurrentAsyncBody result;
    // The old owner validates every promise register use, every await, and the
    // exact implicit catch. Its scope is unchanged; only proved host-notify
    // suffixes have been projected. It cannot accept a new coroutine shape.
    step(projected.instructions.size());
    auto normalized = ProveAsyncCompletion(projected, frame, budget.bytes, &result.origins,
        &result.awaits, profile, ResourceFailureScope::kExcluded);
    const auto to_raw = [&](abc::CodeOffset offset) {
      auto found = std::lower_bound(projected.instructions.begin(), projected.instructions.end(), offset,
          [](const auto& instruction, auto target) { return instruction.offset < target; });
      if (found == projected.instructions.end() || found->offset != offset) fail(ConcurrentError::kCorrespondence);
      return raw_pcs[found - projected.instructions.begin()];
    };
    if (auto* error = std::get_if<AsyncCompletionFailure>(&normalized)) {
      if (error->async.location.pc) error->async.location.pc = to_raw(*error->async.location.pc);
      return *error;
    }
    result.body = std::get<abc::MethodBody>(std::move(normalized));
    // Normalizer's byte budget includes temporary indexing. Retained output
    // arenas remain admitted in this caller while raw/projected arenas live.
    admit(result.body.instructions.capacity(), sizeof(abc::BodyInstruction));
    admit(result.body.tries.capacity(), sizeof(abc::TryRegion)); admit(result.body.handlers.capacity(), sizeof(abc::CatchHandler));
    admit(result.origins.capacity(), sizeof(abc::CodeOffset)); admit(result.awaits.capacity(), sizeof(analysis::AwaitSite));
    for (auto& origin : result.origins) { step(1); origin = to_raw(origin); }
    for (auto& await : result.awaits) {
      step(analysis::kAwaitProtocolInstructions + 9);
      auto& origin = await.origin;
      for (auto* offset : {&origin.await, &origin.suspend, &origin.resume, &origin.result_store, &origin.mode,
                          &origin.mode_store, &origin.branch, &origin.reject, &origin.continuation}) *offset = to_raw(*offset);
      if (origin.instructions) for (auto& offset : *origin.instructions) offset = to_raw(offset);
    }
    result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
    return result;
  } catch (const ConcurrentFailure& error) { return error; }
  catch (const std::bad_alloc&) { ConcurrentFailure e; e.kind = ConcurrentError::kResourceLimit; return e; }
  catch (const std::length_error&) { ConcurrentFailure e; e.kind = ConcurrentError::kResourceLimit; return e; }
}
std::variant<std::uint64_t, ConcurrentFailure> VerifyConcurrentAsync(
    const ConcurrentAsyncEvidence& evidence, const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  auto built = NormalizeConcurrentAsync(evidence.original, evidence.frame, evidence.profile, budget);
  if (const auto* error = std::get_if<ConcurrentFailure>(&built)) return *error;
  ConcurrentFailure failure; failure.kind = ConcurrentError::kCorrespondence;
  if (const auto* error = std::get_if<AsyncCompletionFailure>(&built)) {
    failure.kind = error->kind == FunctionEmitError::kResourceLimit ? ConcurrentError::kResourceLimit : ConcurrentError::kCorrespondence;
    failure.pc = error->async.location.pc; return failure;
  }
  const auto& normalized = std::get<ConcurrentAsyncBody>(built);
  if (normalized.work > budget.work || graph.ops.size() > budget.work - normalized.work) { failure.kind = ConcurrentError::kWorkLimit; return failure; }
  if (graph.ops.size() != normalized.body.instructions.size() || graph.origins.size() != graph.ops.size()) return failure;
  // A normalization origin is not permission to erase another op. Require a
  // complete bijection, including dead sites; returns must be actual raw
  // settlement ports, and no notification survives as an unrelated value op.
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& operation = graph.ops[i];
    if (operation.origin.value >= graph.origins.size()) return failure;
    const auto& origin = graph.origins[operation.origin.value];
    if (origin.site.value != i || origin.pc != normalized.body.instructions[i].offset ||
        origin.original != normalized.origins[i] || std::holds_alternative<analysis::ValueConcurrentNotification>(operation.operation)) return failure;
    const auto lifted = analysis::LiftSemanticInstruction(normalized.body.instructions[i], evidence.profile, evidence.frame.registers, &evidence.frame);
    const auto* raw = std::get_if<analysis::SemanticInstruction>(&lifted);
    const bool returned = raw && std::holds_alternative<analysis::ReturnOperation>(raw->operation);
    if (operation.life == analysis::ValueLife::kLive &&
        returned != std::holds_alternative<analysis::ValueReturn>(operation.operation)) return failure;
  }
  return normalized.work + graph.ops.size();
}
}  // namespace abcd::source
