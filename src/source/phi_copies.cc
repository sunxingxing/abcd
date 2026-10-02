#include "phi_copy_checks.h"
#include "literal_plan.h"
#include "tree_checks.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
std::variant<PhiCopies, PhiCopyError> BuildCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance, PhiFlow flow,
    PhiDomain domain = PhiDomain::kLanguage) {
  try {
    CopyBudget budget(allowance);
    CheckPhiFlow(graph, budget, flow);
    RequirePhiCopy(graph.uses.size() <= std::numeric_limits<std::uint32_t>::max(), PhiCopyError::kResourceLimit);
    std::size_t count = 0;
    for (const auto& use : graph.uses) { budget.Step(); count += PhiInput(use); }
    PhiCopies result{graph.revision, graph.cfg_revision, {}, {}, {}};
    result.flow = flow;
    result.domain = domain;
    budget.Admit(1, sizeof(result));
    budget.Reserve(result.inputs, count);
    budget.Reserve(result.copies, count);
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step();
      const auto& use = graph.uses[i];
      if (!PhiInput(use)) continue;
      CheckPhiDomain(graph, use, domain);
      result.inputs.push_back({static_cast<std::uint32_t>(i)});
    }
    std::sort(result.inputs.begin(), result.inputs.end(), [&](auto a, auto b) {
      budget.Step();
      const auto& left = *graph.uses[a.value].incoming;
      const auto& right = *graph.uses[b.value].incoming;
      return SameIncoming(left, right) ? a < b : IncomingLess(left, right);
    });
    std::size_t bundles = 0;
    for (std::size_t i = 0; i < result.inputs.size(); ++i) {
      budget.Step();
      const auto& use = graph.uses[result.inputs[i].value];
      if (i == 0 || !SameIncoming(*use.incoming, *graph.uses[result.inputs[i - 1].value].incoming)) ++bundles;
      result.copies.push_back({std::get<analysis::ValueId>(use.owner), use.operand});
    }
    budget.Reserve(result.bundles, bundles);
    std::size_t schedule_bytes = 0;
    for (std::size_t first = 0; first < count;) {
      budget.Step();
      const auto key = *graph.uses[result.inputs[first].value].incoming;
      auto end = first + 1;
      while (end < count && SameIncoming(key, *graph.uses[result.inputs[end].value].incoming)) { budget.Step(); ++end; }
      auto built = BuildEdgeCopies(std::span<const ParallelCopy>{result.copies}.subspan(first, end - first), budget.Remaining());
      auto* schedule = std::get_if<EdgeCopies>(&built);
      if (!schedule) throw PhiScheduleError(std::get<CopyError>(built));
      budget.Admit(schedule->allocated_bytes, 1); budget.Step(schedule->work);
      schedule_bytes += schedule->allocated_bytes; // Already bounded by the shared byte grant.
      result.bundles.push_back({key, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(end - first), std::move(*schedule)});
      first = end;
    }
    const auto checked = VerifyPhiCopiesForFlow(graph, result, budget.Remaining(), flow);
    if (const auto* error = std::get_if<PhiCopyError>(&checked)) return *error;
    budget.Step(std::get<std::uint64_t>(checked));
    result.allocated_bytes = sizeof(result) + schedule_bytes +
        result.inputs.capacity() * sizeof(analysis::UseId) + result.copies.capacity() * sizeof(ParallelCopy) +
        result.bundles.capacity() * sizeof(PhiBundle);
    result.work = budget.Work();
    return result;
  } catch (PhiCopyError error) { return error;
  } catch (CopyError error) { return PhiScheduleError(error);
  } catch (const std::bad_alloc&) { return PhiCopyError::kResourceLimit;
  } catch (const std::length_error&) { return PhiCopyError::kResourceLimit; }
}
}  // namespace
std::variant<PhiCopies, PhiCopyError> BuildPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance) {
  return BuildCopies(graph, allowance, PhiFlow::kNormal);
}
std::variant<PhiCopies, PhiCopyError> BuildPhasedPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance, PhiDomain domain) {
  return BuildCopies(graph, allowance, PhiFlow::kPhased, domain);
}
std::vector<std::optional<analysis::OpId>> IndexPhiLiteralEnds(
    const analysis::ValueProgram& graph, const LiteralPlan& literals, TreeCheckBudget& budget) {
  std::vector<std::optional<analysis::OpId>> ends;
  if (!graph.phases.empty()) return ends;
  budget.Array(ends, graph.ops.size());
  for (const auto& object : literals.objects) {
    budget.Step(); if (object.form != LiteralForm::kExpression) continue;
    const auto& allocation = graph.ops[object.allocation.value];
    auto last = object.allocation; bool same_block = true;
    for (std::uint32_t i = 0; i < object.count; ++i) {
      budget.Step(); const auto id = literals.definitions[object.first + i];
      same_block &= graph.ops[id.value].block == allocation.block;
      last = std::max(last, id);
    }
    if (same_block) ends[object.allocation.value] = last;
  }
  return ends;
}
std::optional<analysis::OpId> PhiExpressionProducer(
    const analysis::ValueProgram& graph, const PhiCopies& copies, const PhiBundle& bundle,
    std::span<const std::optional<analysis::OpId>> module_guards,
    std::span<const std::optional<analysis::OpId>> literal_ends) {
  using namespace analysis;
  if (copies.flow != PhiFlow::kNormal || bundle.count != 1 ||
      bundle.schedule.steps.size() != 1 || bundle.schedule.temporaries != 0) return {};
  const auto* edge = std::get_if<EdgeId>(&bundle.incoming);
  if (!edge) return {};
  const auto input = copies.inputs[bundle.first];
  const auto& use = graph.uses[input.value];
  const auto* value = std::get_if<ValueId>(&use.operand);
  if (!value) return {};
  const auto* producer = std::get_if<OpResult>(&graph.values[value->value].origin);
  if (!producer) return {};
  const auto& op = graph.ops[producer->op.value];
  const auto guard = std::holds_alternative<ValueModuleRead>(op.operation) &&
      !module_guards.empty() ? module_guards[producer->op.value] : std::nullopt;
  const auto literal = literal_ends.empty() ? std::nullopt : literal_ends[producer->op.value];
  bool found = false;
  for (auto id = graph.values[value->value].first_use; id; id = graph.uses[id->value].next) {
    const auto& occurrence = graph.uses[id->value];
    if (occurrence.life != ValueLife::kLive) continue;
    if (*id == input) { if (found) return {}; found = true; }
    else if (!guard || *id != graph.ops[guard->value].first_use) {
      const auto* owner = std::get_if<OpId>(&occurrence.owner);
      if (!literal || !owner || *owner <= producer->op || *owner > *literal ||
          occurrence.role != OperandRole::kBase || *id != graph.ops[owner->value].first_use ||
          (!std::holds_alternative<ValueOwnWrite>(graph.ops[owner->value].operation) &&
           !std::holds_alternative<ValueIndexedWrite>(graph.ops[owner->value].operation))) return {};
    }
  }
  if (!found) return {};
  const auto& block = graph.blocks[op.block.value];
  if (op.life != ValueLife::kLive || block.edge_count != 1 || block.first_edge != *edge ||
      graph.edges[edge->value].source != op.block) return {};
  // Only inert control may follow. In particular environment changes, getters,
  // stores and additional roots remain barriers even if the result is unused.
  for (std::uint32_t order = op.order + 1; order < block.op_count; ++order) {
    // Fresh LiteralPlan proves all initialization/RHS effects in this interval
    // belong to the one native object expression. Never skip an arbitrary
    // suffix, staged definition, escape, or independent observable root.
    if (literal && block.first_op.value + order <= literal->value) continue;
    // IndexModuleEvaluations proved adjacent read/guard operands and effects.
    // Retain both at this phi expression; no other throwing suffix may move.
    if (guard && block.first_op.value + order == guard->value) continue;
    const auto& later = graph.ops[block.first_op.value + order];
    if (later.life != ValueLife::kLive) continue;
    if ((!std::holds_alternative<ValueJump>(later.operation) &&
         !std::holds_alternative<ValueNop>(later.operation)) || later.result || later.use_count ||
        later.normal != LanguageEffect::kNone || later.before_throw != LanguageEffect::kNone ||
        later.exceptional != ExceptionalCompletion::kImpossible) return {};
  }
  return producer->op;
}
}  // namespace abcd::source
