#include "binding_plan.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<BindingPlan, BindingError> BuildBindingPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = BindingError;
  const auto verified = VerifyEvaluationPlan(graph, evaluation, budget);
  if (const auto* error = std::get_if<EvaluationError>(&verified)) {
    if (*error == EvaluationError::kResourceLimit) return Error::kResourceLimit;
    if (*error == EvaluationError::kWorkLimit) return Error::kWorkLimit;
    return Error::kEvaluation;
  }
  auto work = budget.work - std::get<std::uint64_t>(verified);
  auto bytes = budget.bytes;
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > bytes / width) return false;
    bytes -= count * width;
    return true;
  };
  // Upper bound one snapshot per operation; never grow vectors during mapping.
  constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
  if (graph.ops.size() > kMaxId - parameter_count ||
      graph.values.size() > kMaxId ||
      !admit(1, sizeof(BindingPlan)) ||
      !admit(graph.values.size(), sizeof(ValueBinding)) ||
      !admit(graph.ops.size() + parameter_count, sizeof(BindingSource)))
    return Error::kResourceLimit;
  try {
    BindingPlan plan;
    plan.revision = graph.revision;
    plan.cfg_revision = graph.cfg_revision;
    plan.parameter_count = parameter_count;
    plan.values.resize(graph.values.size());
    const auto symbol_limit = graph.ops.size() + parameter_count;
    plan.symbols.reserve(symbol_limit);
    if (!admit(plan.values.capacity() - graph.values.size(), sizeof(ValueBinding)) ||
        !admit(plan.symbols.capacity() - symbol_limit, sizeof(BindingSource)))
      return Error::kResourceLimit;
    for (std::uint32_t i = 0; i < parameter_count; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      plan.symbols.emplace_back(ParameterBinding{i});
    }
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      const auto& value = graph.values[i];
      if (value.life == ValueLife::kRemoved) continue;
      if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
        if (const auto* parameter = std::get_if<ParameterEntry>(entry)) {
          if (parameter->index >= parameter_count) return Error::kParameter;
          plan.values[i] = SymbolId{parameter->index};
        } else if (std::holds_alternative<ReceiverEntry>(*entry)) {
          plan.values[i] = ReceiverBinding{};
        } else if (value.first_use) {
          // Unused callable/new-target ABI entries need no source declaration.
          // A live read cannot be relabeled as an ordinary undefined local.
          return Error::kUnsupportedEntry;
        }
        continue;
      }
      if (std::holds_alternative<PhiDefinition>(value.origin))
        return Error::kLivePhi;
      const auto producer = std::get<OpResult>(value.origin).op;
      if (producer.value >= graph.ops.size() ||
          graph.ops[producer.value].result != ValueId{static_cast<std::uint32_t>(i)})
        return Error::kDefinition;
      const auto& placement = evaluation.operations[producer.value];
      if (!placement.temporary) continue;
      const SymbolId symbol{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(SnapshotBinding{
          ValueId{static_cast<std::uint32_t>(i)}, producer, *placement.temporary});
      plan.values[i] = symbol;
    }
    plan.allocated_bytes = budget.bytes - bytes;
    plan.work = budget.work - work;
    return plan;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
