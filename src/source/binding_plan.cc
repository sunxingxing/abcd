#include "binding_plan.h"
#include "evaluation_flow.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
std::uint32_t CycleCount(const EvaluationPlan& evaluation, std::size_t bundle) {
  // Exceptional copies use separate staging slots, never the abstract cycle
  // schedule's destinations or temporary locations before the throwing op.
  if (evaluation.exceptions &&
      std::holds_alternative<ThrowCopies>(evaluation.exceptions->placements[bundle])) return 0;
  return evaluation.phis->bundles[bundle].schedule.temporaries;
}
std::variant<BindingPlan, BindingError> BuildBindings(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget, EvaluationFlow flow) {
  using namespace analysis;
  using Error = BindingError;
  const auto verified = VerifyEvaluationForFlow(graph, evaluation, budget, flow);
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
  std::size_t phi_count = 0, cycle_count = 0, catch_count = 0, update_count = 0;
  bool needs_self = false;
  for (const auto& op : graph.ops) {
    if (work == 0) return Error::kWorkLimit;
    --work;
    needs_self |= op.life == ValueLife::kLive && std::holds_alternative<ValueFrameFunction>(op.operation);
    const auto kind = evaluation.operations[static_cast<std::size_t>(&op - graph.ops.data())].kind;
    update_count += op.life == ValueLife::kLive && kind != EvaluationKind::kElidedPure && NeedsNumericScratch(graph, op);
  }
  const std::size_t self_count = needs_self ? 1 : 0;
  const std::size_t hole_count = NeedsHoleToken(evaluation) ? 1 : 0;
  const auto cell_count = evaluation.cells ? evaluation.cells->cells.cells.size() : 0;
  const auto slot_count = evaluation.exceptions ? evaluation.exceptions->slots.size() : 0;
  for (const auto& value : graph.values) {
    if (work == 0) return Error::kWorkLimit;
    --work;
    phi_count += value.life == ValueLife::kLive && std::holds_alternative<PhiDefinition>(value.origin);
    catch_count += value.life == ValueLife::kLive && std::holds_alternative<CatchDefinition>(value.origin);
  }
  if (evaluation.phis) for (std::size_t i = 0; i < evaluation.phis->bundles.size(); ++i) {
    if (work == 0) return Error::kWorkLimit;
    --work;
    const auto count = CycleCount(evaluation, i);
    if (count > kMaxId - cycle_count) return Error::kResourceLimit;
    cycle_count += count;
  }
  std::size_t fixed_symbols = parameter_count;
  for (const auto count : {phi_count, cycle_count, self_count, catch_count, slot_count, hole_count, update_count}) {
    if (count > kMaxId - fixed_symbols) return Error::kResourceLimit;
    fixed_symbols += count;
  }
  const auto bundles = evaluation.phis ? evaluation.phis->bundles.size() : 0;
  if (bundles > kMaxId || cell_count > kMaxId - fixed_symbols ||
      graph.ops.size() > kMaxId - fixed_symbols - cell_count ||
      graph.values.size() > kMaxId ||
      !admit(1, sizeof(BindingPlan)) ||
      !admit(graph.values.size(), sizeof(ValueBinding)) ||
      !admit(bundles, sizeof(std::optional<SymbolId>)) ||
      !admit(cell_count, sizeof(SymbolId)) ||
      !admit(slot_count, sizeof(SymbolId)) ||
      !admit(update_count ? graph.ops.size() : 0, sizeof(std::optional<SymbolId>)) ||
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
    plan.flow = flow;
    plan.revision = graph.revision;
    plan.cfg_revision = graph.cfg_revision;
    plan.parameter_count = parameter_count;
    plan.parameters = std::move(*parameters);
    plan.values.resize(graph.values.size());
    const auto symbol_limit = graph.ops.size() + fixed_symbols + cell_count;
    plan.symbols.reserve(symbol_limit);
    plan.cycle_bases.resize(bundles);
    plan.cells.resize(cell_count);
    plan.catch_slots.resize(slot_count);
    if (update_count) plan.updates.resize(graph.ops.size());
    if (!admit(plan.values.capacity() - graph.values.size(), sizeof(ValueBinding)) ||
        !admit(plan.cycle_bases.capacity() - bundles, sizeof(std::optional<SymbolId>)) ||
        !admit(plan.cells.capacity() - cell_count, sizeof(SymbolId)) ||
        !admit(plan.catch_slots.capacity() - slot_count, sizeof(SymbolId)) ||
        !admit(plan.updates.capacity() - plan.updates.size(), sizeof(std::optional<SymbolId>)) ||
        !admit(plan.symbols.capacity() - symbol_limit, sizeof(BindingSource)))
      return Error::kResourceLimit;
    for (std::uint32_t i = 0; i < parameter_count; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      plan.symbols.emplace_back(ParameterBinding{i});
      if (const auto entry = plan.parameters.formals[i].entry)
        plan.values[entry->value] = SymbolId{i};
    }
    if (needs_self) {
      plan.self = SymbolId{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(FunctionSelfBinding{});
    }
    if (NeedsHoleToken(evaluation)) {
      plan.hole = SymbolId{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(HoleTokenBinding{});
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
        } else {
          // Unused callable/new-target ABI entries need no source declaration.
          // Checked projections do not read the entry in generated source.
          // Alias verification never roots an alias at an unsupported entry.
          // A retained read cannot be relabeled as an undefined local.
          for (auto use = value.first_use; use; use = graph.uses[use->value].next) {
            if (!work) return Error::kWorkLimit;
            --work;
            const auto* owner = std::get_if<OpId>(&graph.uses[use->value].owner);
            // Common evaluation capability retains the invocation identity.
            // It grants no source local or constructor equivalence; derived
            // projection owns that proof. Never exempt newTarget consumers.
            const auto shape = owner ? DescribeEvaluation(graph.ops[owner->value].operation) : std::nullopt;
            if (shape && shape->invocation_constructor && std::holds_alternative<CallableEntry>(*entry) &&
                graph.uses[use->value].role == OperandRole::kConstructor)
              continue;
            if (!owner || (evaluation.operations[owner->value].kind != EvaluationKind::kElidedPure &&
                           evaluation.operations[owner->value].kind != EvaluationKind::kAlias))
              return Error::kUnsupportedEntry;
          }
        }
        continue;
      }
      if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
        if (!evaluation.logical_values.empty() && evaluation.logical_values[i]) continue;
        if (!evaluation.phis) return Error::kLivePhi;
        const SymbolId symbol{static_cast<std::uint32_t>(plan.symbols.size())};
        plan.symbols.emplace_back(PhiBinding{ValueId{static_cast<std::uint32_t>(i)}, phi->block});
        plan.values[i] = symbol;
        continue;
      }
      if (const auto* caught = std::get_if<CatchDefinition>(&value.origin)) {
        if (flow != EvaluationFlow::kPhased || !value.domain.IsLanguage() ||
            caught->block.value >= graph.phases.size() ||
            graph.phases[caught->block.value].phase != ExceptionPhase::kCatch) return Error::kDefinition;
        const SymbolId symbol{static_cast<std::uint32_t>(plan.symbols.size())};
        plan.symbols.emplace_back(CatchParameterBinding{ValueId{static_cast<std::uint32_t>(i)}, caught->block});
        plan.values[i] = symbol;
        continue;
      }
      const auto producer = std::get<OpResult>(value.origin).op;
      if (producer.value >= graph.ops.size() ||
          graph.ops[producer.value].result != ValueId{static_cast<std::uint32_t>(i)})
        return Error::kDefinition;
      const auto& placement = evaluation.operations[producer.value];
      if (!placement.temporary || IsAbsentHeritageTransport(graph, evaluation, graph.ops[producer.value])) continue;
      const SymbolId symbol{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(SnapshotBinding{
          ValueId{static_cast<std::uint32_t>(i)}, producer, *placement.temporary});
      plan.values[i] = symbol;
    }
    // Roots already own the binding; aliases add neither storage nor writes.
    for (std::size_t i = 0; i < evaluation.aliases.size(); ++i) {
      if (!work) return Error::kWorkLimit;
      --work;
      plan.values[i] = plan.values[evaluation.aliases[i].value];
    }
    if (update_count) for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (!work) return Error::kWorkLimit;
      --work;
      const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || evaluation.operations[i].kind == EvaluationKind::kElidedPure ||
          !NeedsNumericScratch(graph, op)) continue;
      plan.updates[i] = SymbolId{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(UpdateBinding{OpId{static_cast<std::uint32_t>(i)}});
    }
    for (std::size_t i = 0; i < bundles; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      const auto count = CycleCount(evaluation, i);
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
    for (std::size_t i = 0; i < slot_count; ++i) {
      if (work == 0) return Error::kWorkLimit;
      --work;
      plan.catch_slots[i] = SymbolId{static_cast<std::uint32_t>(plan.symbols.size())};
      plan.symbols.emplace_back(CatchSlotBinding{CatchSlotId{static_cast<std::uint32_t>(i)}});
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
}  // namespace
std::variant<BindingPlan, BindingError> BuildBindingPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget) {
  return BuildBindings(graph, evaluation, parameter_count, budget, EvaluationFlow::kNormal);
}
std::variant<BindingPlan, BindingError> BuildPhasedBindingPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, analysis::SemanticBudget budget) {
  return BuildBindings(graph, evaluation, parameter_count, budget, EvaluationFlow::kPhased);
}
}  // namespace abcd::source
