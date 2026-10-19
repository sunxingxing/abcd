#include "literal_event_view.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool condition, LiteralEventError error) {
  if (!condition) throw error;
}
// One parent edge per original operand/definition. Memoized root resolution
// rejects cycles without rescanning a nested initializer for each ancestor.
LiteralEventView Derive(const ValueProgram& graph, const LiteralPlan& plan,
    TreeCheckBudget& budget) {
  Require(plan.revision == graph.revision &&
      plan.cfg_revision == graph.cfg_revision, LiteralEventError::kRevision);
  LiteralEventView result{graph.revision, graph.cfg_revision, {}};
  std::vector<std::optional<OpId>> parents, roots;
  std::vector<std::optional<LiteralEventRole>> roles;
  std::vector<std::uint8_t> state;
  std::vector<OpId> stack;
  budget.Array(result.events, graph.ops.size());
  budget.Array(parents, graph.ops.size());
  budget.Array(roots, graph.ops.size());
  budget.Array(roles, graph.ops.size());
  budget.Array(state, graph.ops.size());
  budget.Array(stack, graph.ops.size());
  const auto valid = [&](OpId id) {
    Require(id.value < graph.ops.size() &&
        graph.ops[id.value].life == ValueLife::kLive,
        LiteralEventError::kCoverage);
  };
  for (const auto& object : plan.objects) {
    budget.Step();
    if (object.form != LiteralForm::kExpression) continue;
    valid(object.allocation);
    Require(!roles[object.allocation.value], LiteralEventError::kOwnership);
    roles[object.allocation.value] = LiteralEventRole::kAllocation;
    Require(object.first <= plan.definitions.size() &&
        object.count <= plan.definitions.size() - object.first,
        LiteralEventError::kCoverage);
    for (std::size_t i = 0; i < object.count; ++i) {
      budget.Step();
      const auto definition = plan.definitions[object.first + i];
      valid(definition);
      Require(!roles[definition.value] && !parents[definition.value],
          LiteralEventError::kOwnership);
      roles[definition.value] = LiteralEventRole::kDefinition;
      parents[definition.value] = object.allocation;
    }
  }
  Require(plan.nested.empty() || plan.nested.size() == graph.ops.size(),
      LiteralEventError::kCoverage);
  for (std::size_t i = 0; i < plan.nested.size(); ++i) {
    budget.Step();
    if (!plan.nested[i]) continue;
    const OpId id{static_cast<std::uint32_t>(i)};
    valid(id);
    const auto use = *plan.nested[i];
    Require(use.value < graph.uses.size() &&
        graph.uses[use.value].life == ValueLife::kLive,
        LiteralEventError::kCoverage);
    const auto* owner = std::get_if<OpId>(&graph.uses[use.value].owner);
    Require(owner != nullptr, LiteralEventError::kOwnership);
    valid(*owner);
    Require(!parents[i], LiteralEventError::kOwnership);
    parents[i] = *owner;
    if (!roles[i]) roles[i] = LiteralEventRole::kOperand;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (state[i]) continue;
    std::size_t depth = 0, at = i;
    while (!state[at]) {
      budget.Step();
      state[at] = 1;
      stack[depth++] = OpId{static_cast<std::uint32_t>(at)};
      if (!parents[at]) {
        if (roles[at] == LiteralEventRole::kAllocation)
          roots[at] = OpId{static_cast<std::uint32_t>(at)};
        break;
      }
      at = parents[at]->value;
    }
    Require(!parents[at] || state[at] == 2, LiteralEventError::kOwnership);
    const auto root = roots[at];
    while (depth) {
      budget.Step();
      const auto id = stack[--depth];
      roots[id.value] = root;
      state[id.value] = 2;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (!roots[i]) continue;
    Require(roles[i].has_value(), LiteralEventError::kOwnership);
    const auto& op = graph.ops[i];
    std::optional<WorkBlockId> commit;
    if (op.result) {
      Require(op.result->value < graph.values.size(), LiteralEventError::kCoverage);
      const auto* origin = std::get_if<OpResult>(&graph.values[op.result->value].origin);
      Require(origin && origin->op.value == i, LiteralEventError::kCorrespondence);
      commit = origin->commit;
    }
    result.events[i] = LiteralEvent{OpId{static_cast<std::uint32_t>(i)},
        *roots[i], *roles[i], plan.nested.empty() ? std::nullopt : plan.nested[i],
        commit, {}};
  }
  // Definitions often have no SSA result; their Commit is a phase identity,
  // not an invented result value. Retain it independently of OpResult.
  for (std::size_t b = 0; b < graph.phases.size(); ++b) {
    budget.Step();
    const auto& phase = graph.phases[b];
    if (phase.phase != ExceptionPhase::kCommit || !phase.operation) continue;
    Require(phase.operation->value < result.events.size(), LiteralEventError::kCoverage);
    auto& event = result.events[phase.operation->value];
    if (!event) continue;
    const WorkBlockId commit{static_cast<std::uint32_t>(b)};
    Require(!event->commit || event->commit == commit, LiteralEventError::kCorrespondence);
    event->commit = commit;
  }
  for (std::size_t i = 0; i < graph.exceptional_exits.size(); ++i) {
    budget.Step();
    const auto& exit = graph.exceptional_exits[i];
    Require(exit.op.value < graph.ops.size(), LiteralEventError::kCoverage);
    auto& event = result.events[exit.op.value];
    if (!event) continue;
    Require(!event->exception_exit && graph.ops[exit.op.value].exceptional ==
        ExceptionalCompletion::kMayThrow, LiteralEventError::kCorrespondence);
    event->exception_exit = i;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (!result.events[i]) continue;
    Require(result.events[i]->exception_exit.has_value() ==
        (graph.ops[i].exceptional == ExceptionalCompletion::kMayThrow),
        LiteralEventError::kCorrespondence);
  }
  return result;
}
}  // namespace
LiteralEventView BuildLiteralEventView(const analysis::ValueProgram& graph,
    const LiteralPlan& plan, TreeCheckBudget& budget) {
  return Derive(graph, plan, budget);
}
void VerifyLiteralEventView(const analysis::ValueProgram& graph,
    const LiteralPlan& plan, const LiteralEventView& view, TreeCheckBudget& budget) {
  Require(view.revision == graph.revision && view.cfg_revision == graph.cfg_revision,
      LiteralEventError::kRevision);
  const auto expected = Derive(graph, plan, budget);
  Require(view.events.size() == expected.events.size(), LiteralEventError::kCoverage);
  for (std::size_t i = 0; i < expected.events.size(); ++i) {
    budget.Step();
    Require(view.events[i] == expected.events[i], LiteralEventError::kCorrespondence);
  }
}
}  // namespace abcd::source
