#include "exception_copy_emission.h"

#include <algorithm>

#include "effect_order.h"
#include "evaluation_liveness.h"
#include "logical_regions.h"
#include "phase_predicate_prefix.h"

namespace abcd::source {
namespace {
using namespace analysis;
constexpr std::uint32_t kEmptySnapshot = 0;
void Require(bool condition) {
  if (!condition) throw EvaluationError::kCatchState;
}
template<class T> void Reserve(std::vector<T>& rows, std::size_t count,
                              TreeCheckBudget& budget) {
  budget.Admit(count, sizeof(T)); rows.reserve(count);
  budget.Admit(rows.capacity() - count, sizeof(T));
}
bool Ordinary(const EvaluationPlan& plan, std::size_t copy) {
  return plan.exceptions->staging[copy].has_value();
}
// Inline ownership alone excludes selected arms/prefixes. Add their verified
// source enclosures, then resolve this functional forest once (no CFG walks
// per variable or expression). Selection/control verifiers still own execution.
std::vector<OpId> Owners(const ValueProgram& graph, const EvaluationPlan& plan,
                         TreeCheckBudget& budget) {
  auto roots = IndexPhaseInlineRoots(graph, plan, budget);
  std::vector<OpId> parents;
  budget.Array(parents, graph.ops.size());
  for (std::size_t i = 0; i < parents.size(); ++i) parents[i] = roots[i];
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (plan.operations[i].kind != EvaluationKind::kCellGuard) continue;
    Require(plan.cells.has_value());
    const auto input = InitializedCellGuardInput(graph, *plan.cells, OpId{static_cast<std::uint32_t>(i)});
    Require(input.has_value());
    const auto* load = std::get_if<OpResult>(&graph.values[input->value].origin); Require(load != nullptr);
    parents[i] = roots[load->op.value];
  }
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  std::vector<std::optional<std::size_t>> index;
  budget.Array(index, graph.ops.size());
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); index[regions[i].branch.value] = i;
  }
  for (const auto& selected : plan.phase_selections) {
    budget.Step(); Require(index[selected.branch.value].has_value());
    const auto& region = regions[*index[selected.branch.value]];
    const auto enclose = [&](OpId id) {
      const auto root = roots[id.value];
      if (root != selected.execution) parents[root.value] = selected.execution;
    };
    enclose(selected.branch);
    for (const auto prefix : selected.prefix) { budget.Step(); enclose(prefix); }
    for (const auto& test : region.conditions) { budget.Step(); enclose(test.branch); }
    for (const auto* arm : {&region.right, region.consequent ? &*region.consequent : nullptr}) {
      budget.Step(); if (!arm) continue;
      if (const auto* value = std::get_if<ValueId>(arm))
        if (const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin)) enclose(result->op);
    }
  }
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<Visit> state;
  std::vector<OpId> stack;
  budget.Array(state, graph.ops.size()); budget.Array(stack, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (state[i] == Visit::kDone) continue;
    auto at = OpId{static_cast<std::uint32_t>(i)}; std::size_t depth = 0;
    while (state[at.value] == Visit::kUnseen) {
      budget.Step(); state[at.value] = Visit::kActive; stack[depth++] = at;
      Require(parents[at.value].value < graph.ops.size());
      if (parents[at.value] == at) break;
      at = parents[at.value];
    }
    Require(state[at.value] == Visit::kDone || parents[at.value] == at);
    const auto root = state[at.value] == Visit::kDone ? roots[at.value] : at;
    while (depth) { budget.Step(); at = stack[--depth]; roots[at.value] = root; state[at.value] = Visit::kDone; }
  }
  return roots;
}
}  // namespace

std::size_t ExceptionCopyEmission::AllocatedBytes() const {
  return groups.capacity() * sizeof(StageGroup) + assignments.capacity() * sizeof(StageAssignment) +
      coverage.capacity() * sizeof(StageCoverage) +
      (bundles.capacity() + entries.capacity()) * sizeof(std::optional<std::uint32_t>);
}
std::vector<std::optional<std::uint32_t>> IndexExceptionSnapshots(
    const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<std::uint32_t>> result;
  budget.Array(result, graph.ops.size());
  for (const auto& exit : graph.exceptional_exits) { budget.Step(); result[exit.op.value] = kEmptySnapshot; }
  if (!plan.exceptions) return result;
  struct Row { std::uint32_t bundle, first, count; };
  std::vector<Row> rows;
  std::vector<StageAssignment> cells;
  Reserve(rows, plan.phis->bundles.size(), budget);
  Reserve(cells, plan.phis->copies.size(), budget);
  for (std::size_t b = 0; b < plan.phis->bundles.size(); ++b) {
    budget.Step(); if (!std::holds_alternative<ThrowCopies>(plan.exceptions->placements[b])) continue;
    const auto& bundle = plan.phis->bundles[b];
    const auto first = static_cast<std::uint32_t>(cells.size());
    for (std::uint32_t j = 0; j < bundle.count; ++j) {
      budget.Step(); const auto copy = bundle.first + j;
      if (Ordinary(plan, copy)) cells.push_back({*plan.exceptions->staging[copy],
          ResolveEvaluationOperand(plan, plan.phis->copies[copy].source)});
    }
    std::sort(cells.begin() + first, cells.end(), [&](const auto& a, const auto& b) {
      budget.Step(); return a.slot < b.slot;
    });
    rows.push_back({static_cast<std::uint32_t>(b), first, static_cast<std::uint32_t>(cells.size()) - first});
  }
  const auto less = [&](const Row& a, const Row& b) {
    budget.Step();
    if (a.count != b.count) return a.count < b.count;
    for (std::uint32_t i = 0; i < a.count; ++i) {
      budget.Step(); const auto& x = cells[a.first + i]; const auto& y = cells[b.first + i];
      if (x.slot != y.slot) return x.slot < y.slot;
      if (!SameStageOperand(x.source, y.source)) {
        const auto key = [](const ValueOperand& operand) {
          const auto bits = std::visit([](const auto& value) -> std::uint64_t {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, SpecialValue>)
              return static_cast<std::uint64_t>(value);
            else return static_cast<std::uint64_t>(value.value);
          }, operand);
          return std::pair{operand.index(), bits};
        };
        return key(x.source) < key(y.source);
      }
    }
    return false;
  };
  std::sort(rows.begin(), rows.end(), less);
  std::uint32_t signature = kEmptySnapshot;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    budget.Step(); if (rows[i].count && (!i || !rows[i - 1].count || less(rows[i - 1], rows[i]))) ++signature;
    result[std::get<ThrowCopies>(plan.exceptions->placements[rows[i].bundle]).operation.value] = signature;
  }
  return result;
}
ExceptionCopyEmission BuildExceptionCopyEmission(const ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget) {
  ExceptionCopyEmission result;
  if (plan.exception_stage_domain == ExceptionStageDomain::kOriginal) return result;
  Require(plan.exception_stage_domain == ExceptionStageDomain::kUniform && plan.flow == EvaluationFlow::kPhased);
  if (!plan.exceptions) return result;
  const auto owners = Owners(graph, plan, budget);
  const auto snapshots = IndexExceptionSnapshots(graph, plan, budget);
  auto built = BuildEffectOrder(graph, budget.Remaining());
  if (const auto* error = std::get_if<ValueAvailabilityError>(&built)) {
    if (*error == ValueAvailabilityError::kWorkLimit) throw EvaluationError::kWorkLimit;
    if (*error == ValueAvailabilityError::kResourceLimit) throw EvaluationError::kResourceLimit;
    throw EvaluationError::kUnavailableValue;
  }
  const auto& order = std::get<EffectOrder>(built);
  budget.Admit(order.allocated_bytes, 1); budget.Step(order.work);
  std::vector<std::optional<std::uint32_t>> group_of;
  std::vector<std::optional<OpId>> entries;
  std::vector<std::uint8_t> needed;
  budget.Array(group_of, graph.ops.size()); budget.Array(entries, graph.ops.size());
  budget.Array(needed, graph.ops.size());
  // Choose the shallowest actual member Before port. Dominance below checks
  // every member; physical operation/PC order supplies no movement permission.
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (graph.ops[i].life != ValueLife::kLive) continue;
    const OpId id{static_cast<std::uint32_t>(i)}; const auto root = owners[i];
    auto& entry = entries[root.value]; const auto& op = graph.ops[i];
    if (!entry || order.dominators.nodes[op.block.value].begin <
        order.dominators.nodes[graph.ops[entry->value].block.value].begin ||
        (op.block == graph.ops[entry->value].block && op.order < graph.ops[entry->value].order)) entry = id;
  }
  for (std::size_t b = 0; b < plan.phis->bundles.size(); ++b) {
    budget.Step(); const auto* stage = std::get_if<ThrowCopies>(&plan.exceptions->placements[b]);
    if (stage && plan.phis->bundles[b].count && owners[stage->operation.value] != stage->operation)
      needed[owners[stage->operation.value].value] = 1;
  }
  Reserve(result.groups, graph.ops.size(), budget);
  Reserve(result.assignments, plan.phis->copies.size(), budget);
  Reserve(result.coverage, plan.phis->copies.size(), budget);
  budget.Array(result.bundles, plan.phis->bundles.size()); budget.Array(result.entries, graph.ops.size());
  for (std::size_t b = 0; b < plan.phis->bundles.size(); ++b) {
    budget.Step(); const auto* stage = std::get_if<ThrowCopies>(&plan.exceptions->placements[b]);
    if (!stage || !needed[owners[stage->operation.value].value]) continue;
    const auto root = owners[stage->operation.value];
    const auto& bundle = plan.phis->bundles[b];
    auto& group = group_of[root.value];
    if (!group) {
      group = static_cast<std::uint32_t>(result.groups.size());
      const auto entry = *entries[root.value]; const auto& start = graph.ops[entry.value];
      const auto first = static_cast<std::uint32_t>(result.assignments.size());
      for (std::uint32_t j = 0; j < bundle.count; ++j) {
        budget.Step(); const auto copy = bundle.first + j;
        if (!Ordinary(plan, copy)) continue;
        const auto source = ResolveEvaluationOperand(plan, plan.phis->copies[copy].source);
        if (const auto* value = std::get_if<ValueId>(&source))
          Require(HasRetainedEvaluationValue(graph, plan, *value) &&
              ValueAvailableAt(graph, order.dominators, *value, start.block, start.order));
        result.assignments.push_back({*plan.exceptions->staging[copy], source});
      }
      result.groups.push_back({root, entry, stage->handler, static_cast<std::uint32_t>(b), first,
          static_cast<std::uint32_t>(result.assignments.size()) - first});
      Require(!result.entries[entry.value]); result.entries[entry.value] = group;
    }
    const auto& shared = result.groups[*group];
    const auto& representative = std::get<ThrowCopies>(plan.exceptions->placements[shared.bundle]);
    Require(shared.handler == stage->handler && snapshots[representative.operation.value] == snapshots[stage->operation.value]);
    result.bundles[b] = group;
    // Find each slot in the representative's sorted index, with exact source
    // equality independently guaranteed by the interned snapshot signature.
    for (std::uint32_t j = 0; j < bundle.count; ++j) {
      budget.Step(); const auto copy = bundle.first + j;
      if (!Ordinary(plan, copy)) continue;
      // Original catch-phi order is not slot order; coverage is explicitly indexed.
      result.coverage.push_back({static_cast<std::uint32_t>(b), copy, 0, *group, stage->operation, stage->edge});
    }
  }
  // Index slots once per group, not a scan of its snapshot for every throw.
  std::vector<std::pair<std::pair<std::uint32_t, CatchSlotId>, std::uint32_t>> slots;
  Reserve(slots, result.assignments.size(), budget);
  for (std::uint32_t g = 0; g < result.groups.size(); ++g) {
    const auto& group = result.groups[g];
    for (std::uint32_t j = 0; j < group.count; ++j) {
      budget.Step(); const auto a = group.first + j;
      slots.push_back({{g, result.assignments[a].slot}, a});
    }
  }
  std::sort(slots.begin(), slots.end(), [&](const auto& a, const auto& b) { budget.Step(); return a.first < b.first; });
  for (auto& cover : result.coverage) {
    budget.Step(); const auto key = std::pair{cover.group, *plan.exceptions->staging[cover.copy]};
    const auto at = std::lower_bound(slots.begin(), slots.end(), key, [&](const auto& a, auto b) { budget.Step(); return a.first < b; });
    Require(at != slots.end() && at->first == key); cover.assignment = at->second;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto group = group_of[owners[i].value]; if (!group) continue;
    const auto& shared = result.groups[*group]; const auto& start = graph.ops[shared.entry.value];
    const auto& op = graph.ops[i];
    Require(order.dominators.Dominates({start.block.value}, {op.block.value}) &&
        (start.block != op.block || start.order <= op.order));
    Require(!std::holds_alternative<ValueAwait>(op.operation) && !std::holds_alternative<ValueYield>(op.operation));
    if (op.exceptional == ExceptionalCompletion::kImpossible) continue;
    const auto& representative = std::get<ThrowCopies>(plan.exceptions->placements[shared.bundle]);
    Require(snapshots[i] && snapshots[i] == snapshots[representative.operation.value]);
  }
  return result;
}
void VerifyExceptionCopyEmission(const ValueProgram& graph, const EvaluationPlan& plan,
                                 TreeCheckBudget& budget) {
  const auto expected = BuildExceptionCopyEmission(graph, plan, budget);
  budget.Step(expected.groups.size() + expected.assignments.size() + expected.coverage.size() +
      expected.bundles.size() + expected.entries.size());
  Require(plan.exception_emission == expected);
}
}  // namespace abcd::source
