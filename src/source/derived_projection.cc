#include "derived_projection.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error = DerivedProjectionError;
void Require(bool condition, Error error) { if (!condition) throw error; }
enum class State : std::uint8_t { kUnseen, kActive, kOther, kUndefined, kSuper, kConstructor };
}
std::variant<DerivedProjection, Error> ProjectDerivedConstructor(
    const FileFunction& function, const FunctionContext& owner, analysis::SemanticBudget budget) {
  using namespace analysis;
  try {
    Require(owner.Callable() == CallableKind::kDerivedConstructor && owner.ConstructorSite() &&
        function.input.MatchesOwner(owner) && function.frame == owner.Frame(), Error::kOwner);
    bool spread = false;
    for (const auto& instruction : function.input.Input().instructions)
      spread |= std::holds_alternative<SuperSpread>(instruction.operation);
    // Select from the actual protocol, never retry a failed range proof under
    // a more permissive form. Both share normal-flow initialization analysis.
    const auto proof = [&]() -> std::variant<abc::CodeOffset, DerivedConstructorError> {
      if (spread) {
        const auto checked = ProveSpreadDerivedConstructor(function.body, function.frame,
            {budget.bytes, budget.work}, function.input.Identity().profile);
        if (const auto* error = std::get_if<DerivedConstructorError>(&checked)) return *error;
        return std::get<SpreadDerivedConstructor>(checked).super_pc;
      }
      const auto checked = ProveRangeDerivedConstructor(function.body, function.frame,
          {budget.bytes, budget.work}, function.input.Identity().profile);
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
    // One cached walk per value, no guard-by-guard traversal of shared chains.
    constexpr auto width = sizeof(State) + sizeof(ValueId);
    Require(graph.values.size() <= budget.bytes / width, Error::kResourceLimit);
    std::vector<State> states(graph.values.size(), State::kUnseen);
    std::vector<ValueId> path; path.reserve(graph.values.size());
    Require(states.capacity() <= budget.bytes / sizeof(State), Error::kResourceLimit);
    budget.bytes -= states.capacity() * sizeof(State);
    Require(path.capacity() <= budget.bytes / sizeof(ValueId), Error::kResourceLimit);
    budget.bytes -= path.capacity() * sizeof(ValueId);
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
    const auto classify = [&](ValueOperand operand) {
      path.clear(); State state = State::kOther;
      while (const auto* id = std::get_if<ValueId>(&operand)) {
        step(); Require(id->value < graph.values.size(), Error::kOperand);
        const auto& value = graph.values[id->value];
        Require(value.life == ValueLife::kLive, Error::kOperand);
        if (states[id->value] != State::kUnseen) {
          state = states[id->value]; Require(state != State::kActive, Error::kOperand); break;
        }
        states[id->value] = State::kActive; path.push_back(*id);
        // The actual derived invocation starts with the VM's undefined
        // receiver. This is an owner premise, never a general ABI inference.
        if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
          if (std::holds_alternative<ReceiverEntry>(*entry)) state = State::kUndefined;
          else if (std::holds_alternative<CallableEntry>(*entry)) state = State::kConstructor;
          break;
        }
        const auto* origin = std::get_if<OpResult>(&value.origin);
        if (!origin) break;
        Require(origin->op.value < graph.ops.size(), Error::kOrigin);
        const auto& op = graph.ops[origin->op.value];
        Require(op.life == ValueLife::kLive && op.result == *id, Error::kOrigin);
        if (origin->op == result.call) { state = State::kSuper; break; }
        if (!std::holds_alternative<ValueCopy>(op.operation)) break;
        Require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
            op.exceptional == ExceptionalCompletion::kImpossible && op.use_count == 1 &&
            op.first_use.value < graph.uses.size(), Error::kOperand);
        operand = graph.uses[op.first_use.value].operand;
      }
      if (const auto* literal = std::get_if<SpecialValue>(&operand); literal && *literal == SpecialValue::kUndefined)
        state = State::kUndefined;
      for (const auto id : path) { step(); states[id.value] = state; }
      return state;
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
    analysis::SemanticBudget budget) {
  if (projection.revision != function.values.graph.revision ||
      projection.cfg_revision != function.values.graph.cfg_revision) return Error::kRevision;
  const auto checked = ProjectDerivedConstructor(function, owner, budget);
  if (const auto* error = std::get_if<Error>(&checked)) return *error;
  const auto& expected = std::get<DerivedProjection>(checked);
  if (expected.call != projection.call || expected.guards != projection.guards) return Error::kCoverage;
  return {};
}

}
