#include "class_accessors.h"
#include "../analysis/value_edits.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<ClassAccessorSites, ClassAccessorError> FindClassAccessors(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ClassAccessorError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error = Error::kInput) { if (!ok) throw error; };
    const auto step = [&](std::size_t count = 1) {
      require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      require(count <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= count * width;
    };
    require(graph.phases.empty(), Error::kFlow);
    step(graph.ops.size()); step(graph.values.size()); step(graph.uses.size());
    admit(graph.uses.size(), sizeof(std::uint8_t));
    require(!VerifyValueUses(graph, graph.uses.size()));
    const auto input = [&](OpId id, std::uint32_t ordinal, OperandRole role, ReadPurpose purpose = ReadPurpose::kOperand) -> const ValueOperand& {
      require(id.value < graph.ops.size()); const auto& op = graph.ops[id.value];
      require(ordinal < op.use_count);
      const auto& use = graph.uses[op.first_use.value + ordinal];
      require(use.role == role && use.purpose == purpose);
      return use.operand;
    };
    const auto producer = [&](const ValueOperand& operand) -> std::optional<OpId> {
      const auto* value = std::get_if<ValueId>(&operand);
      if (!value) return {};
      require(value->value < graph.values.size());
      const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
      if (!result) return {};
      require(result->op.value < graph.ops.size() && graph.ops[result->op.value].life == ValueLife::kLive);
      return result->op;
    };
    std::size_t count = 0;
    for (const auto& op : graph.ops) { step(); count += op.life == ValueLife::kLive && std::holds_alternative<ValueDefineAccessor>(op.operation); }
    ClassAccessorSites result{graph.revision};
    admit(count, sizeof(ClassAccessorSite)); result.sites.reserve(count);
    admit(result.sites.capacity() - count, sizeof(ClassAccessorSite));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueDefineAccessor>(op.operation)) continue;
      require(i <= UINT32_MAX && op.use_count == 5 && op.result);
      const OpId id{static_cast<std::uint32_t>(i)};
      const auto& base = input(id, 0, OperandRole::kBase);
      const auto* home = std::get_if<ValueId>(&base);
      auto owner = producer(base);
      const auto key = producer(input(id, 1, OperandRole::kKey));
      const ValueOperand* flag_value = &input(id, 4, OperandRole::kNameFlag, ReadPurpose::kProtocol);
      // Protocol reads intentionally retain SSA identity during simplification.
      // Inspect a pure defining literal copy without rewriting that occurrence.
      if (const auto flag_op = producer(*flag_value); flag_op &&
          std::holds_alternative<ValueCopy>(graph.ops[flag_op->value].operation) &&
          graph.ops[flag_op->value].use_count == 1 && CanErasePureOp(graph.ops[flag_op->value]))
        flag_value = &input(*flag_op, 0, OperandRole::kInput);
      const auto* flag = std::get_if<SpecialValue>(flag_value);
      if (!home || !owner || !key || !flag || *flag != SpecialValue::kFalse) continue;
      const auto* name = std::get_if<ValueString>(&graph.ops[key->value].operation);
      if (!name) continue;
      std::optional<OpId> prototype;
      if (std::holds_alternative<ValueNamedRead>(graph.ops[owner->value].operation)) {
        prototype = owner;
        owner = producer(input(*owner, 0, OperandRole::kBase));
      }
      if (!owner || !std::holds_alternative<ValueClassCreation>(graph.ops[owner->value].operation)) continue;
      if (graph.ops[owner->value].block != op.block || graph.ops[owner->value].order >= op.order) continue;
      std::optional<OpId> methods[2]; bool valid = true;
      for (std::uint32_t n = 0; n < 2; ++n) {
        step(); const auto& value = input(id, 2 + n, n ? OperandRole::kSetter : OperandRole::kGetter);
        if (const auto* absent = std::get_if<SpecialValue>(&value); absent && *absent == SpecialValue::kUndefined) continue;
        const auto method_id = producer(value);
        if (!method_id) { valid = false; break; }
        const auto& method = graph.ops[method_id->value];
        const auto* creation = std::get_if<ValueFunctionCreation>(&method.operation);
        if (!creation || creation->kind != CreationKind::kMethod || creation->home_object != HomeObjectInput::kAccumulator ||
            creation->length != n || method.block != op.block || method.order >= op.order || method.use_count != 1 ||
            producer(input(*method_id, 0, OperandRole::kHomeObject, ReadPurpose::kProtocol)) != producer(base)) { valid = false; break; }
        require(method.result.has_value());
        const auto use = graph.values[method.result->value].first_use;
        // Simplification has removed register copies. The function object must
        // be consumed exactly once by this definition, not escape or be reused.
        if (!use || use->value != op.first_use.value + 2 + n || graph.uses[use->value].next) { valid = false; break; }
        methods[n] = method_id;
      }
      if (valid && (methods[0] || methods[1])) result.sites.push_back({*owner, id, *home, prototype, name->string, methods[0], methods[1]});
    }
    result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
