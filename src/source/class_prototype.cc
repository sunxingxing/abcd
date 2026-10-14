#include "class_prototype.h"
#include "../analysis/value_phi.h"
#include "../analysis/value_flow.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
bool HasClassPrototypeKey(const ClassPrototypeRead& site, ExpressionConstants constants,
    std::span<const std::u16string> names, std::span<const std::u16string> texts) {
  const auto id = site.key.value_or(site.read);
  if (id.value >= constants.operations.size()) return false;
  const auto& constant = constants.operations[id.value];
  if (site.key) {
    const auto* text = std::get_if<TextId>(&constant);
    return text && text->value < constants.text_count && text->value < texts.size() && texts[text->value] == u"prototype";
  }
  const auto* name = std::get_if<NameId>(&constant);
  return name && name->value < constants.name_count && name->value < names.size() && names[name->value] == u"prototype";
}
std::variant<ClassPrototypeReads, ClassPrototypeError> FindClassPrototypeReads(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ClassPrototypeError;
  const auto initial = budget;
  try {
    const auto step = [&](std::uint64_t n = 1) {
      if (n > budget.work) throw Error::kWorkLimit;
      budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      if (n > budget.bytes / width) throw Error::kResourceLimit;
      budget.bytes -= n * width;
    };
    auto verified = VerifyValueFlow(graph, budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&verified)) {
      if (*error == ValueAvailabilityError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    const auto& flow = std::get<ValueFlowProof>(verified);
    admit(flow.allocated_bytes, 1); step(flow.work);
    admit(graph.uses.size(), sizeof(std::uint8_t));
    step(graph.values.size()); step(graph.ops.size()); step(graph.uses.size()); step(graph.uses.size()); step(graph.uses.size());
    if (const auto error = VerifyValueUses(graph, graph.uses.size()))
      return *error == ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kFlow;
    auto analyzed = AnalyzeValueIdentities(graph, budget);
    if (const auto* error = std::get_if<ValueSimplifyError>(&analyzed)) {
      if (*error == ValueSimplifyError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueSimplifyError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    const auto& identities = std::get<ValueIdentities>(analyzed);
    admit(identities.allocated_bytes, 1); step(identities.work);
    const auto producer = [&](ValueId value) -> std::optional<OpId> {
      const auto root = identities.canonical[value.value];
      const auto* origin = std::get_if<OpResult>(&graph.values[root.value].origin);
      return origin ? std::optional<OpId>{origin->op} : std::nullopt;
    };
    ClassPrototypeReads result;
    admit(graph.ops.size(), sizeof(ClassPrototypeRead)); result.reads.reserve(graph.ops.size());
    admit(result.reads.capacity() - graph.ops.size(), sizeof(ClassPrototypeRead));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !op.result) continue;
      const bool computed = std::holds_alternative<ValuePropertyRead>(op.operation);
      if (!computed && (!std::holds_alternative<ValueNamedRead>(op.operation) ||
          std::get<ValueNamedRead>(op.operation).lookup != PropertyLookup::kObject)) continue;
      std::optional<OpId> base, key;
      for (std::uint32_t j = 0; j < op.use_count; ++j) {
        step(); const auto& use = graph.uses[op.first_use.value + j];
        const auto* value = std::get_if<ValueId>(&use.operand);
        if (!value) continue;
        if (use.role == OperandRole::kBase) base = producer(*value);
        if (use.role == OperandRole::kKey) key = producer(*value);
      }
      if (!base || !std::holds_alternative<ValueClassCreation>(graph.ops[base->value].operation)) continue;
      if (computed && (!key || !std::holds_alternative<ValueString>(graph.ops[key->value].operation))) continue;
      result.reads.push_back({OpId{static_cast<std::uint32_t>(i)}, *base, computed ? key : std::nullopt});
    }
    result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
