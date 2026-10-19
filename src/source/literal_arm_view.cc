#include "literal_arm_view.h"
#include "evaluation_aliases.h"

namespace abcd::source {
namespace {
using namespace analysis;
bool ControlOnly(const ValueOp& op) {
  const auto shape = DescribeEvaluation(op.operation);
  return shape && shape->control != ControlEvaluation::kNone &&
      op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible &&
      (std::holds_alternative<ValueJump>(op.operation) ||
       std::holds_alternative<ValueBranch>(op.operation));
}
}
LiteralArmIndex::LiteralArmIndex(const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget) : graph_(graph), plan_(plan), budget_(budget) {
  if (!plan.literals || !graph.phases.empty()) throw LiteralEventError::kCorrespondence;
  view_ = BuildLiteralEventView(graph, *plan.literals, budget);
  auto checked = VerifyValueFlow(graph, budget.Remaining());
  if (const auto* error = std::get_if<ValueAvailabilityError>(&checked)) {
    if (*error == ValueAvailabilityError::kResourceLimit) throw TreeStructureError::kResourceLimit;
    if (*error == ValueAvailabilityError::kWorkLimit) throw TreeStructureError::kWorkLimit;
    throw LiteralEventError::kCorrespondence;
  }
  auto& flow = std::get<ValueFlowProof>(checked);
  budget.Admit(flow.allocated_bytes, 1); budget.Step(flow.work);
  dominators_ = std::move(flow.dominators);
  budget.Array(counts_, graph.ops.size()); budget.Array(first_, graph.ops.size() + 1);
  budget.Array(cursor_, graph.ops.size()); budget.Array(external_, graph.ops.size());
  budget.Array(escaped_, graph.ops.size()); budget.Array(cached_, graph.ops.size());
  budget.Array(prefix_, graph.blocks.size() + 2); budget.Array(blocks_, graph.blocks.size() + 1);
  budget.Array(incoming_, graph.blocks.size()); budget.Array(indegree_, graph.blocks.size());
  budget.Array(queue_, graph.blocks.size());
  std::size_t total = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (view_.events[i]) { ++counts_[view_.events[i]->allocation.value]; ++total; }
    const auto& op = graph.ops[i];
    if (op.life == ValueLife::kLive && !ControlOnly(op))
      ++prefix_[dominators_.nodes[op.block.value].begin + 1];
  }
  for (std::size_t i = 0; i < graph.blocks.size(); ++i) {
    budget.Step(); const auto begin = dominators_.nodes[i].begin;
    if (begin) blocks_[begin] = WorkBlockId{static_cast<std::uint32_t>(i)};
  }
  for (std::size_t i = 1; i < prefix_.size(); ++i) { budget.Step(); prefix_[i] += prefix_[i - 1]; }
  for (std::size_t i = 0; i < counts_.size(); ++i) {
    budget.Step(); first_[i + 1] = first_[i] + counts_[i]; cursor_[i] = first_[i];
  }
  budget.Array(members_, total);
  for (std::size_t i = 0; i < view_.events.size(); ++i) {
    budget.Step(); if (view_.events[i])
      members_[cursor_[view_.events[i]->allocation.value]++] = OpId{static_cast<std::uint32_t>(i)};
  }
  for (const auto& edge : graph.edges) { budget.Step(); ++incoming_[edge.target.value]; }
  // The only external observation must be this original phi edge. LiteralPlan
  // separately proves nested use identities, no escape and definition order.
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step(); const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    if (const auto* owner = std::get_if<OpId>(&use.owner)) {
      if (graph.ops[owner->value].life != ValueLife::kLive ||
          plan.operations[owner->value].kind == EvaluationKind::kAlias ||
          plan.operations[owner->value].kind == EvaluationKind::kElidedPure) continue;
    }
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    const auto* origin = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!origin || !counts_[origin->op.value]) continue;
    const auto root = origin->op;
    if (const auto* owner = std::get_if<OpId>(&use.owner);
        owner && view_.events[owner->value] && view_.events[owner->value]->allocation == root) continue;
    if (external_[root.value]) escaped_[root.value] = 1;
    external_[root.value] = UseId{static_cast<std::uint32_t>(i)};
  }
}
LiteralArmResult LiteralArmIndex::Prove(EdgeId entry, UseId phi) {
  budget_.Step();
  if (entry.value >= graph_.edges.size() || phi.value >= graph_.uses.size()) return LiteralArmError::kCorrespondence;
  const auto operand = ResolveEvaluationOperand(plan_, graph_.uses[phi.value].operand);
  const auto* value = std::get_if<ValueId>(&operand);
  const auto* origin = value ? std::get_if<OpResult>(&graph_.values[value->value].origin) : nullptr;
  if (!origin || !counts_[origin->op.value]) return LiteralArmError::kCoverage;
  const auto root = origin->op;
  if (escaped_[root.value] || external_[root.value] != phi) return LiteralArmError::kEscape;
  // A root has one source occurrence. A second boundary is not a second proof
  // of that occurrence; reject instead of rescanning an overlapping region.
  if (cached_[root.value]) {
    const auto* old = std::get_if<LiteralArmView>(&*cached_[root.value]);
    if (old && old->entry != entry) return LiteralArmError::kCorrespondence;
    return *cached_[root.value];
  }
  auto result = Derive(root, entry, phi); cached_[root.value] = result; return result;
}
LiteralArmResult LiteralArmIndex::Derive(OpId root, EdgeId entry, UseId phi) {
  const auto& occurrence = graph_.uses[phi.value];
  const auto* merged = std::get_if<ValueId>(&occurrence.owner);
  const auto* exit = occurrence.incoming ? std::get_if<EdgeId>(&*occurrence.incoming) : nullptr;
  if (!merged || !exit || occurrence.role != OperandRole::kPhiInput ||
      exit->value >= graph_.edges.size()) return LiteralArmError::kCorrespondence;
  const auto start = graph_.edges[entry.value].target;
  const auto tail = graph_.edges[exit->value].source;
  const auto join = graph_.edges[exit->value].target;
  const auto range = dominators_.nodes[start.value];
  const auto inside = [&](WorkBlockId block) {
    return dominators_.Dominates({start.value}, {block.value});
  };
  if (!range.begin || incoming_[start.value] != 1 || !inside(tail) || inside(join)) return LiteralArmError::kCoverage;
  if (prefix_[range.end] - prefix_[range.begin] != counts_[root.value]) return LiteralArmError::kCoverage;
  for (auto i = first_[root.value]; i < first_[root.value + 1]; ++i) {
    budget_.Step(); const auto id = members_[i]; const auto& op = graph_.ops[id.value];
    if (!inside(op.block) || !dominators_.Dominates({op.block.value}, {tail.value})) return LiteralArmError::kOrder;
    const auto& event = *view_.events[id.value];
    if (event.exception_exit && graph_.exceptional_exits[*event.exception_exit].handler)
      return LiteralArmError::kCatchState; // Normal route: every throw propagates to caller.
  }
  // Coverage equality plus membership above proves no foreign operation is
  // absorbed. Kahn on this disjoint dominated range proves no re-entry/cycle;
  // every normal path exits on the ORIGINAL phi edge. No layout/PC condition.
  for (auto p = range.begin; p < range.end; ++p) { budget_.Step(); indegree_[blocks_[p].value] = 0; }
  for (auto p = range.begin; p < range.end; ++p) {
    budget_.Step(); const auto& block = graph_.blocks[blocks_[p].value];
    if (!block.edge_count) return LiteralArmError::kCoverage;
    for (std::uint32_t e = 0; e < block.edge_count; ++e) {
      budget_.Step(); const EdgeId id{block.first_edge.value + e}; const auto& edge = graph_.edges[id.value];
      if (edge.kind == FlowEdgeKind::kException) return LiteralArmError::kCatchState;
      if (inside(edge.target)) ++indegree_[edge.target.value];
      else if (id != *exit) return LiteralArmError::kCoverage;
    }
  }
  std::size_t begin = 0, end = 0;
  for (auto p = range.begin; p < range.end; ++p) {
    budget_.Step(); const auto b = blocks_[p]; if (!indegree_[b.value]) queue_[end++] = b;
  }
  while (begin < end) {
    budget_.Step(); const auto& block = graph_.blocks[queue_[begin++].value];
    for (std::uint32_t e = 0; e < block.edge_count; ++e) {
      budget_.Step(); const auto target = graph_.edges[block.first_edge.value + e].target;
      if (inside(target) && --indegree_[target.value] == 0) queue_[end++] = target;
    }
  }
  if (end != range.end - range.begin) return LiteralArmError::kOrder;
  return LiteralArmView{root, *graph_.ops[root.value].result, entry, *exit, phi, counts_[root.value]};
}
void LiteralArmIndex::Verify(const LiteralArmView& supplied) {
  LiteralArmIndex independent(graph_, plan_, budget_);
  const auto derived = independent.Prove(supplied.entry, supplied.phi);
  const auto* expected = std::get_if<LiteralArmView>(&derived);
  if (!expected || *expected != supplied) throw LiteralEventError::kCorrespondence;
}
}  // namespace abcd::source
