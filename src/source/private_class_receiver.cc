#include "private_class_receiver.h"
#include "../analysis/value_phi.h"
#include "effect_order.h"
#include "literal_plan.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<PrivateClassReceiverProof, PrivateClassReceiverError>
BuildPrivateClassReceiver(const analysis::ValueProgram& graph,
                          analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = PrivateClassReceiverError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error e) { if (!ok) throw e; };
    const auto step = [&](std::size_t n = 1) {
      require(n <= budget.work, Error::kWorkLimit); budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t w) {
      require(n <= budget.bytes / w, Error::kResourceLimit); budget.bytes -= n * w;
    };
    require(graph.phases.empty() && !graph.arguments && graph.ops.size() <= UINT32_MAX, Error::kInput);
    admit(1, sizeof(PrivateClassReceiverProof));
    auto ordered = BuildEffectOrder(graph, budget);
    if (const auto* e = std::get_if<ValueAvailabilityError>(&ordered)) {
      require(false, *e == ValueAvailabilityError::kResourceLimit ? Error::kResourceLimit :
          *e == ValueAvailabilityError::kWorkLimit ? Error::kWorkLimit : Error::kInput);
    }
    const auto& order = std::get<EffectOrder>(ordered);
    admit(order.allocated_bytes, 1); step(order.work);
    auto indexed = AnalyzeValueIdentities(graph, budget);
    if (const auto* e = std::get_if<ValueSimplifyError>(&indexed)) {
      require(false, *e == ValueSimplifyError::kResourceLimit ? Error::kResourceLimit :
          *e == ValueSimplifyError::kWorkLimit ? Error::kWorkLimit : Error::kInput);
    }
    const auto& ids = std::get<ValueIdentities>(indexed);
    admit(ids.allocated_bytes, 1); step(ids.work);
    const auto receiver = [&](const ValueOperand& operand) {
      const auto* id = std::get_if<ValueId>(&operand);
      return id && IsReceiverOperand(graph, ValueOperand{ids.canonical[id->value]});
    };
    PrivateClassReceiverProof result;
    result.evidence.revision = graph.revision;
    result.evidence.control = graph.cfg_revision;
    const auto reserve = [&](auto& vector, std::size_t count) {
      using T = typename std::decay_t<decltype(vector)>::value_type;
      admit(count, sizeof(T)); vector.reserve(count);
      admit(vector.capacity() - count, sizeof(T));
    };
    reserve(result.evidence.effects, graph.ops.size());
    reserve(result.evidence.fields, graph.ops.size());
    reserve(result.evidence.exceptions, graph.exceptional_exits.size());
    admit(graph.ops.size(), sizeof(std::uint8_t));
    std::vector<std::uint8_t> exits(graph.ops.size());
    admit(exits.capacity() - exits.size(), sizeof(std::uint8_t));
    for (const auto& exit : graph.exceptional_exits) {
      step();
      require(exit.op.value < graph.ops.size() && !exits[exit.op.value] &&
          !exit.handler, Error::kInput);
      const auto& op = graph.ops[exit.op.value];
      require(op.life == ValueLife::kLive &&
          op.exceptional != ExceptionalCompletion::kImpossible &&
          exit.origin == op.origin && exit.before_throw == op.before_throw, Error::kInput);
      exits[exit.op.value] = 1;
      result.evidence.exceptions.push_back({exit.op, exit.origin, exit.before_throw});
    }
    std::size_t effects = 0;
    bool returned = false;
    for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive) continue;
      require(bool(exits[i]) == (op.exceptional != ExceptionalCompletion::kImpossible), Error::kInput);
      effects += HasOrderedEffect(op);
      // A fresh closure/class or explicit environment value can expose an
      // incoming self cell without reading the receiver SSA value. Ordinary
      // private lexical reads/writes and stack changes do not export that
      // environment. The Closed owner separately excludes incoming accesses.
      const auto state = static_cast<std::uint16_t>(op.normal | op.before_throw);
      constexpr auto environment = static_cast<std::uint16_t>(LanguageEffect::kEnvironmentRead);
      constexpr auto local = static_cast<std::uint16_t>(LanguageEffect::kLexicalRead |
          LanguageEffect::kLexicalWrite | LanguageEffect::kEnvironmentWrite);
      constexpr auto reflection = static_cast<std::uint16_t>(LanguageEffect::kDebugger |
          LanguageEffect::kSuspension);
      require(!(state & reflection) && (!(state & environment) || (state & local)), Error::kObservation);
      if (const auto* end = std::get_if<ValueReturn>(&op.operation)) {
        (void)end;
        require(op.use_count == 1, Error::kObservation);
        const auto* value = std::get_if<SpecialValue>(&graph.uses[op.first_use.value].operand);
        require(value && *value == SpecialValue::kUndefined, Error::kObservation);
        returned = true;
      }
    }
    for (const auto& use : graph.uses) {
      step();
      if (use.life != ValueLife::kLive || !receiver(use.operand)) continue;
      if (const auto* phi = std::get_if<ValueId>(&use.owner)) {
        require(receiver(ValueOperand{*phi}), Error::kObservation);
        continue;
      }
      const auto id = std::get<OpId>(use.owner);
      const auto& op = graph.ops[id.value];
      if (std::holds_alternative<ValueCopy>(op.operation) && op.result &&
          receiver(ValueOperand{*op.result})) continue;
      const auto* field = std::get_if<ValueOwnWrite>(&op.operation);
      require(field && field->kind == OwnWriteKind::kDefineField &&
          use.role == OperandRole::kBase && op.use_count == 2 && !op.result &&
          !receiver(graph.uses[op.first_use.value + 1].operand), Error::kObservation);
    }
    // All ordered operations, including RHS calls/reads and each field Commit,
    // form one must-reach chain. No branch can skip, replay or reorder an event.
    std::optional<OpId> next = order.first, previous;
    while (next) {
      step(); const auto id = *next;
      require(result.evidence.effects.size() < effects, Error::kOrder);
      const auto& op = graph.ops[id.value];
      require(HasOrderedEffect(op) && order.before[id.value] ==
          (previous ? EffectPredecessor{*previous} : EffectPredecessor{EntryEffect{}}), Error::kOrder);
      if (previous) require(order.Before(graph, *previous, op.block, op.order), Error::kOrder);
      result.evidence.effects.push_back(id);
      if (const auto* field = std::get_if<ValueOwnWrite>(&op.operation);
          field && field->kind == OwnWriteKind::kDefineField &&
          receiver(graph.uses[op.first_use.value].operand))
        result.evidence.fields.push_back(id);
      previous = id; next = order.after[id.value];
      if (!next) require(order.normal_return[id.value], Error::kOrder);
    }
    require(returned && !result.evidence.fields.empty() &&
        result.evidence.effects.size() == effects, Error::kOrder);
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error e) { return e; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<PrivateClassReceiverProof, PrivateClassReceiverError>
VerifyPrivateClassReceiver(const analysis::ValueProgram& graph,
                          const PrivateClassReceiver& evidence,
                          analysis::SemanticBudget budget) {
  auto result = BuildPrivateClassReceiver(graph, budget);
  if (const auto* proof = std::get_if<PrivateClassReceiverProof>(&result);
      proof && proof->evidence != evidence) return PrivateClassReceiverError::kInput;
  return result;
}
}  // namespace abcd::source
