#include "derived_projection.h"
#include "derived_value_states.h"
#include "abcd/source/capture_preservation.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error = DerivedProjectionError;
void Require(bool condition, Error error) { if (!condition) throw error; }
using State = DerivedValueState;
}
std::variant<DerivedProjection, Error> ProjectDerivedConstructor(
    const FileFunction& function, const FunctionContext& owner, analysis::SemanticBudget budget,
    const DerivedCaptureInput* captures) {
  using namespace analysis;
  try {
    Require(owner.Callable() == CallableKind::kDerivedConstructor && owner.ConstructorSite() &&
        function.input.MatchesOwner(owner) && function.frame == owner.Frame(), Error::kOwner);
    CapturePreservations preserved;
    if (captures) {
      Require(captures->file.Size() == owner.Identity().file_size && !captures->file.CheckUnchanged() &&
          captures->header.file_size == owner.Identity().file_size && captures->header.profile == owner.Identity().profile && captures->header.version == owner.Identity().abc_version &&
          captures->lookup.Find(owner.Target()) == owner.Position() && owner.Position().value < captures->index.methods.size() &&
          captures->index.methods[owner.Position().value].offset == owner.Target() &&
          captures->index.methods[owner.Position().value].owner_offset == function.input.Record(), Error::kOwner);
      MethodConstants constants(captures->file, captures->header, captures->index, owner.Target(), {});
      auto checked = ProveCapturePreservation(function.body, constants, &captures->lookup, budget);
      if (const auto* error = std::get_if<SourceFailure>(&checked)) {
        Require(error->kind != SourceError::kResourceLimit, Error::kResourceLimit);
        throw Error::kProtocol;
      }
      preserved = std::get<CapturePreservations>(std::move(checked));
      Require(preserved.AllocatedBytes() <= budget.bytes && preserved.work_units <= budget.work, Error::kResourceLimit);
      budget.bytes -= preserved.AllocatedBytes(); budget.work -= preserved.work_units;
    }
    bool spread = false;
    for (const auto& instruction : function.input.Input().instructions)
      spread |= std::holds_alternative<SuperSpread>(instruction.operation);
    // Select from the actual protocol, never retry a failed range proof under
    // a more permissive form. Both share normal-flow initialization analysis.
    const auto proof = [&]() -> std::variant<abc::CodeOffset, DerivedConstructorError> {
      if (spread) {
        const auto checked = ProveSpreadDerivedConstructor(function.body, function.frame,
            {budget.bytes, budget.work}, function.input.Identity().profile, preserved.sites);
        if (const auto* error = std::get_if<DerivedConstructorError>(&checked)) return *error;
        return std::get<SpreadDerivedConstructor>(checked).super_pc;
      }
      const auto checked = ProveRangeDerivedConstructor(function.body, function.frame,
          {budget.bytes, budget.work}, function.input.Identity().profile, preserved.sites);
      if (const auto* error = std::get_if<DerivedConstructorError>(&checked)) return *error;
      return std::get<RangeDerivedConstructor>(checked).super_pc;
    }();
    if (const auto* error = std::get_if<DerivedConstructorError>(&proof))
      throw *error == DerivedConstructorError::kResourceLimit ? Error::kResourceLimit : Error::kProtocol;
    Require(budget.bytes >= sizeof(DerivedProjection), Error::kResourceLimit);
    budget.bytes -= sizeof(DerivedProjection);
    const auto& graph = function.values.graph;
    const auto& input = function.input.Input();
    Require(graph.input.sites.size() == input.instructions.size(), Error::kOrigin);
    const auto step = [&] { Require(budget.work != 0, Error::kWorkLimit); --budget.work; };
    DerivedProjection result{graph.revision, graph.cfg_revision, {}};
    bool found = false;
    for (std::size_t i = 0; i < input.instructions.size(); ++i) {
      step();
      const auto& original = input.instructions[i];
      if (original.pc != std::get<abc::CodeOffset>(proof)) continue;
      Require(graph.input.sites[i].has_value(), Error::kOrigin);
      result.call = *graph.input.sites[i]; found = true; break;
    }
    Require(found && result.call.value < graph.ops.size(), Error::kOrigin);
    auto classified = BuildDerivedValueStates(function, result.call, preserved.sites, budget);
    if (const auto* error = std::get_if<Error>(&classified)) throw *error;
    const auto& states = std::get<DerivedValueStates>(classified);
    Require(states.allocated_bytes <= budget.bytes && states.work <= budget.work, Error::kResourceLimit);
    budget.bytes -= states.allocated_bytes; budget.work -= states.work;
    const auto classify = [&](const ValueOperand& operand) {
      step();
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        Require(value->value < states.values.size() && graph.values[value->value].life == ValueLife::kLive, Error::kOperand);
        return states.values[value->value];
      }
      const auto* special = std::get_if<SpecialValue>(&operand);
      return special && *special == SpecialValue::kUndefined ? State::kUndefined : State::kOther;
    };
    std::size_t guard_count = 0;
    for (const auto& original : input.instructions) { step(); guard_count += std::holds_alternative<SuperCheck>(original.operation); }
    Require(guard_count <= budget.bytes / sizeof(OpId), Error::kResourceLimit);
    result.guards.reserve(guard_count);
    Require(result.guards.capacity() <= budget.bytes / sizeof(OpId), Error::kResourceLimit);
    for (std::size_t i = 0; i < input.instructions.size(); ++i) {
      step(); const auto& original = input.instructions[i];
      const auto* check = std::get_if<SuperCheck>(&original.operation);
      const auto* call = std::get_if<SuperRange>(&original.operation);
      const auto* spread_call = std::get_if<SuperSpread>(&original.operation);
      if (!check && !call && !spread_call) continue;
      Require(graph.input.sites[i].has_value(), Error::kOrigin);
      const auto id = *graph.input.sites[i]; Require(id.value < graph.ops.size(), Error::kOrigin);
      const auto& op = graph.ops[id.value];
      Require(op.life == ValueLife::kLive && op.origin.value < graph.origins.size(), Error::kOrigin);
      const auto& origin = graph.origins[op.origin.value];
      Require(origin.site.value == i && origin.pc == original.pc && origin.original == original.origin &&
          origin.disposition == OriginDisposition::kRetained && op.normal == original.effects.normal &&
          op.before_throw == original.effects.before_throw && op.exceptional == original.effects.exceptional,
          Error::kOrigin);
      if (call) {
        const auto* current = std::get_if<ValueSuperCall>(&op.operation);
        Require(id == result.call && current && current->arguments == call->count &&
            current->cache_slot == call->cache_slot && op.result && op.use_count == call->count, Error::kOrigin);
      } else if (spread_call) {
        const auto* current = std::get_if<ValueSuperSpread>(&op.operation);
        Require(id == result.call && current && current->cache_slot == spread_call->cache_slot &&
            op.result && op.use_count == 2 && op.first_use.value <= graph.uses.size() &&
            graph.uses.size() - op.first_use.value >= 2, Error::kOrigin);
        const auto& constructor = graph.uses[op.first_use.value];
        const auto& array = graph.uses[op.first_use.value + 1];
        Require(constructor.life == ValueLife::kLive && array.life == ValueLife::kLive &&
            constructor.owner == decltype(constructor.owner){id} &&
            array.owner == decltype(array.owner){id} &&
            constructor.role == OperandRole::kConstructor && array.role == OperandRole::kInput &&
            constructor.purpose == ReadPurpose::kOperand && array.purpose == ReadPurpose::kOperand &&
            classify(constructor.operand) == State::kConstructor, Error::kOperand);
      } else {
        const auto* current = std::get_if<ValueSuperCheck>(&op.operation);
        Require(current && current->kind == check->kind && !op.result && op.use_count == 1 &&
            op.first_use.value < graph.uses.size(), Error::kOrigin);
        const auto& use = graph.uses[op.first_use.value];
        Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){id} &&
            use.role == OperandRole::kGuardValue && use.purpose == ReadPurpose::kProtocol, Error::kOperand);
        const auto state = classify(use.operand);
        switch (check->kind) {
          case SuperCheckKind::kRequireInitialized: Require(state == State::kSuper, Error::kOperand); break;
          case SuperCheckKind::kRequireUninitialized: Require(state == State::kUndefined, Error::kOperand); break;
          case SuperCheckKind::kNoop: break;
        }
        result.guards.push_back(id);
      }
    }
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::optional<DerivedProjectionError> VerifyDerivedProjection(
    const FileFunction& function, const FunctionContext& owner, const DerivedProjection& projection,
    analysis::SemanticBudget budget, const DerivedCaptureInput* captures) {
  if (projection.revision != function.values.graph.revision ||
      projection.cfg_revision != function.values.graph.cfg_revision) return Error::kRevision;
  const auto checked = ProjectDerivedConstructor(function, owner, budget, captures);
  if (const auto* error = std::get_if<Error>(&checked)) return *error;
  const auto& expected = std::get<DerivedProjection>(checked);
  if (expected.call != projection.call || expected.guards != projection.guards) return Error::kCoverage;
  return {};
}

}
