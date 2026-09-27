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
  // Upper bound one snapshot per operation plus independent phi and cycle
  // locations. Never coalesce these into mutable source parameters/cells.
  constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
  std::size_t phi_count = 0, cycle_count = 0;
  const auto cell_count = evaluation.cells ? evaluation.cells->cells.cells.size() : 0;
  for (const auto& value : graph.values) {
    if (work == 0) return Error::kWorkLimit;
    --work;
    phi_count += value.life == ValueLife::kLive && std::holds_alternative<PhiDefinition>(value.origin);
  }
  if (evaluation.phis) for (const auto& bundle : evaluation.phis->bundles) {
    if (work == 0) return Error::kWorkLimit;
    --work;
    if (bundle.schedule.temporaries > kMaxId - cycle_count) return Error::kResourceLimit;
    cycle_count += bundle.schedule.temporaries;
  }
  if (phi_count > kMaxId - parameter_count || cycle_count > kMaxId - parameter_count - phi_count)
    return Error::kResourceLimit;
  const auto fixed_symbols = parameter_count + phi_count + cycle_count;
  const auto bundles = evaluation.phis ? evaluation.phis->bundles.size() : 0;
  if (bundles > kMaxId || cell_count > kMaxId - fixed_symbols ||
      graph.ops.size() > kMaxId - fixed_symbols - cell_count ||
      graph.values.size() > kMaxId ||
      !admit(1, sizeof(BindingPlan)) ||
      !admit(graph.values.size(), sizeof(ValueBinding)) ||
      !admit(bundles, sizeof(std::optional<SymbolId>)) ||
      !admit(cell_count, sizeof(SymbolId)) ||
      !admit(graph.ops.size() + fixed_symbols + cell_count, sizeof(BindingSource)))
    return Error::kResourceLimit;
  try {
    auto parameter_result = BuildParameterPlan(graph, parameter_count, {bytes, work});
    auto* parameters = std::get_if<ParameterPlan>(&parameter_result);
    if (!parameters) {
      switch (std::get<ParameterPlanError>(parameter_result)) {
        case ParameterPlanError::kResourceLimit: return Error::kResourceLimit;
        case ParameterPlanError::kWorkLimit: return Error::kWorkLimit;
        case ParameterPlanError::kOrdinal:
        case ParameterPlanError::kDuplicateEntry: return Error::kParameter;
      }
      return Error::kParameter;
    }
    bytes -= parameters->allocated_bytes;
    work -= parameters->work;
    BindingPlan plan;
    plan.revision = graph.revision;
    plan.cfg_revision = graph.cfg_revision;
    plan.parameter_count = parameter_count;
    plan.parameters = std::move(*parameters);
    plan.values.resize(graph.values.size());
    const auto symbol_limit = graph.ops.size() + fixed_symbols + cell_count;
    plan.symbols.reserve(symbol_limit);
    plan.cycle_bases.resize(bundles);
    plan.cells.resize(cell_count);
    if (!admit(plan.values.capacity() - graph.values.size(), sizeof(ValueBinding)) ||
        !admit(plan.cycle_bases.capacity() - bundles, sizeof(std::optional<SymbolId>)) ||
        !admit(plan.cells.capacity() - cell_count, sizeof(SymbolId)) ||
        !admit(plan.symbols.capacity() - symbol_limit, sizeof(BindingSource)))
      return Error::kResourceLimit;
    for (std::uint32_t i = 0; i < parameter_count; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      plan.symbols.emplace_back(ParameterBinding{i});
      if (const auto entry = plan.parameters.formals[i].entry)
        plan.values[entry->value] = SymbolId{i};
    }
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      const auto& value = graph.values[i];
      if (value.life == ValueLife::kRemoved) continue;
      if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
        if (const auto* parameter = std::get_if<ParameterEntry>(entry)) {
          if (parameter->index >= parameter_count ||
              plan.parameters.formals[parameter->index].entry !=
                  ValueId{static_cast<std::uint32_t>(i)}) return Error::kParameter;
        } else if (std::holds_alternative<ReceiverEntry>(*entry)) {
          plan.values[i] = ReceiverEntryBinding{};
        } else if (value.first_use) {
          // Unused callable/new-target ABI entries need no source declaration.
          // A live read cannot be relabeled as an ordinary undefined local.
          return Error::kUnsupportedEntry;
        }
        continue;
      }
      if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
        if (!evaluation.phis) return Error::kLivePhi;
        const SymbolId symbol{static_cast<std::uint32_t>(plan.symbols.size())};
        plan.symbols.emplace_back(PhiBinding{ValueId{static_cast<std::uint32_t>(i)}, phi->block});
        plan.values[i] = symbol;
        continue;
      }
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
    for (std::size_t i = 0; i < bundles; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      const auto count = evaluation.phis->bundles[i].schedule.temporaries;
      if (count != 0) plan.cycle_bases[i] = SymbolId{static_cast<std::uint32_t>(plan.symbols.size())};
      for (std::uint32_t temporary = 0; temporary < count; ++temporary) {
        if (work == 0) return Error::kWorkLimit;
        --work;
        plan.symbols.emplace_back(CycleBinding{static_cast<std::uint32_t>(i), CopyTempId{temporary}});
      }
    }
    for (std::size_t i = 0; i < cell_count; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      plan.cells[i] = SymbolId{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(CellBinding{CellId{static_cast<std::uint32_t>(i)}});
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
