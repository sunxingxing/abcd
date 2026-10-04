#include "literal_class_fields.h"
#include "literal_plan.h"
#include "effect_order.h"
#include "../analysis/value_flow.h"
#include "../analysis/register_value_rule.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<LiteralClassFields, LiteralClassFieldError> LiteralClassFields::Build(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = LiteralClassFieldError;
  const auto initial = budget;
  try {
    const auto require = [](bool condition, Error error = Error::kUnsupported) { if (!condition) throw error; };
    const auto step = [&](std::uint64_t count = 1) { require(count <= budget.work,Error::kWorkLimit); budget.work -= count; };
    const auto admit = [&](std::size_t count, std::size_t width) { require(count <= budget.bytes/width,Error::kResourceLimit); budget.bytes -= count*width; };
    const auto checked = BuildEffectOrder(graph,budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&checked)) {
      if (*error == ValueAvailabilityError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    const auto& order = std::get<EffectOrder>(checked);
    step(order.work); admit(order.allocated_bytes,1);
    require(graph.phases.empty() && !graph.arguments);
    std::vector<std::uint8_t> literals;
    admit(graph.values.size(),sizeof(std::uint8_t)); step(graph.values.size()); literals.resize(graph.values.size());
    admit(literals.capacity()-literals.size(),sizeof(std::uint8_t));
    enum class FieldState : std::uint8_t { kAbsent, kPending, kConsumed };
    std::vector<FieldState> fields;
    admit(graph.ops.size(),sizeof(FieldState)); step(graph.ops.size()); fields.resize(graph.ops.size());
    admit(fields.capacity()-fields.size(),sizeof(FieldState));
    const auto literal = [&](const ValueOperand& operand) {
      if (const auto* number = std::get_if<Float64Bits>(&operand)) return detail::FloatNumberKinds(*number).IsNumber();
      const auto* value = std::get_if<ValueId>(&operand);
      return !value || literals[value->value] != 0;
    };
    LiteralClassFields result;
    admit(1,sizeof(result)); admit(graph.ops.size(),sizeof(OpId)); result.fields_.reserve(graph.ops.size());
    admit(result.fields_.capacity()-graph.ops.size(),sizeof(OpId));
    bool returned = false; std::size_t field_count = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive) continue;
      if (const auto* field = std::get_if<ValueOwnWrite>(&op.operation)) {
        require(field->kind == OwnWriteKind::kDefineField);
        constexpr auto kOwnWriteEffects = LanguageEffect::kHeapWrite | LanguageEffect::kUserCode;
        require(op.use_count == 2 && !op.result && op.normal == kOwnWriteEffects &&
            op.before_throw == kOwnWriteEffects && op.exceptional == ExceptionalCompletion::kMayThrow);
        const auto& base = graph.uses[op.first_use.value];
        const auto& value = graph.uses[op.first_use.value+1];
        require(IsReceiverOperand(graph, base.operand) && base.role == OperandRole::kBase &&
            value.role == OperandRole::kStoredValue && literal(value.operand));
        fields[i] = FieldState::kPending; ++field_count;
        continue;
      }
      require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
          op.exceptional == ExceptionalCompletion::kImpossible);
      // Receiver transport keeps its original event/uses. It is never a literal
      // RHS or permission to reread/replay the frame across other operations.
      if (op.result && IsReceiverOperand(graph, ValueOperand{*op.result})) continue;
      if (std::holds_alternative<ValueString>(op.operation)) {
        require(op.result && op.use_count == 0 && graph.values[op.result->value].domain.IsString());
        literals[op.result->value] = 1;
      } else if (std::holds_alternative<ValueCopy>(op.operation)) {
        require(op.result && op.use_count == 1 && literal(graph.uses[op.first_use.value].operand));
        literals[op.result->value] = 1;
      } else if (std::holds_alternative<ValueReturn>(op.operation)) {
        require(!op.result && op.use_count == 1);
        const auto* value = std::get_if<SpecialValue>(&graph.uses[op.first_use.value].operand);
        require(value && *value == SpecialValue::kUndefined); returned = true;
      } else if (std::holds_alternative<ValueBranch>(op.operation)) {
        require(!op.result && op.use_count == 1 && graph.uses[op.first_use.value].role == OperandRole::kCondition);
      } else require((std::holds_alternative<ValueNop>(op.operation) || std::holds_alternative<ValueJump>(op.operation)) &&
          !op.result && op.use_count == 0);
    }
    require(returned && field_count != 0);
    // Shared past identity and future must-reach prove each original definition
    // executes exactly once, in this order, before every normal return. A pure
    // fork is not a new template; bypass, divergence and interposed effects fail.
    std::optional<OpId> next = order.first;
    std::optional<OpId> previous;
    while (next) {
      step(); const auto id = *next;
      require(id.value < fields.size() && fields[id.value] == FieldState::kPending);
      require(order.before[id.value] == (previous ? EffectPredecessor{*previous} : EffectPredecessor{EntryEffect{}}));
      if (previous) require(order.Before(graph,*previous,graph.ops[id.value].block,graph.ops[id.value].order));
      fields[id.value] = FieldState::kConsumed; result.fields_.push_back(id);
      previous = id; next = order.after[id.value];
      if (!next) require(order.normal_return[id.value]);
    }
    require(result.fields_.size() == field_count);
    result.revision_ = graph.revision; result.cfg_revision_ = graph.cfg_revision;
    result.bytes_ = sizeof(result)+result.fields_.capacity()*sizeof(OpId);
    result.work_ = initial.work-budget.work; return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
