#include "literal_arm_view.h"

#include "call_plan.h"
#include "evaluation_aliases.h"
#include "logical_evaluation.h"

namespace abcd::source {
namespace {
using namespace analysis;
bool ControlOnly(const ValueOp& op) {
  const auto shape = DescribeEvaluation(op.operation);
  return shape && shape->control != ControlEvaluation::kNone &&
         op.normal == LanguageEffect::kNone &&
         op.before_throw == LanguageEffect::kNone &&
         op.exceptional == ExceptionalCompletion::kImpossible &&
         (std::holds_alternative<ValueNop>(op.operation) ||
          std::holds_alternative<ValueJump>(op.operation) ||
          std::holds_alternative<ValueBranch>(op.operation));
}
}
LiteralArmIndex::LiteralArmIndex(const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget) : graph_(graph), plan_(plan), budget_(budget) {
  if (!plan.literals) throw LiteralEventError::kCorrespondence;
  view_ = BuildLiteralEventView(graph, *plan.literals, budget);
  const auto accept_flow = [&](auto checked) {
    if (const auto* error = std::get_if<ValueAvailabilityError>(&checked)) {
      if (*error == ValueAvailabilityError::kResourceLimit)
        throw TreeStructureError::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit)
        throw TreeStructureError::kWorkLimit;
      throw LiteralEventError::kCorrespondence;
    }
    std::visit(
        [&](auto& flow) {
          using T = std::decay_t<decltype(flow)>;
          if constexpr (!std::is_same_v<T, ValueAvailabilityError>) {
            budget.Admit(flow.allocated_bytes, 1);
            budget.Step(flow.work);
            dominators_ = std::move(flow.dominators);
          }
        },
        checked);
  };
  if (graph.phases.empty())
    accept_flow(VerifyValueFlow(graph, budget.Remaining()));
  else
    accept_flow(VerifyPhasedValueFlow(graph, budget.Remaining()));
  budget.Array(counts_, graph.ops.size()); budget.Array(first_, graph.ops.size() + 1);
  budget.Array(cursor_, graph.ops.size()); budget.Array(external_, graph.ops.size());
  budget.Array(escaped_, graph.ops.size()); budget.Array(cached_, graph.ops.size());
  budget.Array(prefix_, graph.blocks.size() + 2); budget.Array(blocks_, graph.blocks.size() + 1);
  budget.Array(incoming_, graph.blocks.size()); budget.Array(indegree_, graph.blocks.size());
  budget.Array(queue_, graph.blocks.size());
  if (!graph.phases.empty()) {
    phase_budget_.emplace(budget.Remaining());
    phases_.emplace(graph, plan, *phase_budget_);
    budget.Admit(phase_budget_->Cost().bytes, 1);
    accounted_work_ = phase_budget_->Cost().work;
    budget.Step(accounted_work_);
  }
  std::size_t total = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (view_.events[i]) { ++counts_[view_.events[i]->allocation.value]; ++total; }
    const auto& op = graph.ops[i];
    if (op.life == ValueLife::kLive && !ControlOnly(op) &&
        plan.operations[i].kind != EvaluationKind::kAlias &&
        plan.operations[i].kind != EvaluationKind::kElidedPure)
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
  for (const auto& edge : graph.edges) {
    budget.Step();
    if (edge.kind != FlowEdgeKind::kException) ++incoming_[edge.target.value];
  }
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
    if (event.commit &&
        !dominators_.Dominates({event.commit->value}, {tail.value}))
      return LiteralArmError::kOrder;
    if (event.exception_exit) {
      if (phases_) {
        const bool valid =
            !phases_->HasCatchCopies(id) && phases_->SameCatch(id, root);
        budget_.Step(phase_budget_->Cost().work - accounted_work_);
        accounted_work_ = phase_budget_->Cost().work;
        if (!valid) return LiteralArmError::kCatchState;
      } else if (graph_.exceptional_exits[*event.exception_exit].handler)
        return LiteralArmError::kCatchState;
    }
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
      if (edge.kind == FlowEdgeKind::kException) continue;
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

std::vector<std::optional<UseId>> IndexNestedLiteralInputs(
    const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget) {
  std::vector<std::optional<UseId>> result;
  if (graph.phases.empty() || !plan.literals || !plan.phis) return result;
  std::vector<std::optional<OpId>> fields;
  std::vector<std::optional<UseId>> external;
  std::vector<std::uint8_t> escaped, seeded, selected;
  std::vector<std::optional<EdgeId>> entries;
  budget.Array(fields, graph.ops.size());
  budget.Array(external, graph.ops.size());
  budget.Array(escaped, graph.ops.size());
  budget.Array(seeded, graph.ops.size());
  budget.Array(selected, graph.values.size());
  budget.Array(entries, graph.blocks.size());
  for (const auto& object : plan.literals->objects) {
    budget.Step();
    for (std::uint32_t j = 0; j < object.count; ++j) {
      budget.Step();
      fields[plan.literals->definitions[object.first + j].value] =
          object.allocation;
    }
  }
  for (std::size_t e = 0; e < graph.edges.size(); ++e) {
    budget.Step();
    const auto& edge = graph.edges[e];
    if (edge.kind != FlowEdgeKind::kException)
      entries[edge.target.value] = EdgeId{static_cast<std::uint32_t>(e)};
  }
  const auto producer =
      [&](const ValueOperand& operand) -> std::optional<OpId> {
    const auto resolved = ResolveEvaluationOperand(plan, operand);
    const auto* v = std::get_if<ValueId>(&resolved);
    const auto* r =
        v ? std::get_if<OpResult>(&graph.values[v->value].origin) : nullptr;
    return r ? std::optional{r->op} : std::nullopt;
  };
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step();
    const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (owner &&
        (plan.operations[owner->value].kind == EvaluationKind::kAlias ||
         plan.operations[owner->value].kind == EvaluationKind::kElidedPure))
      continue;
    const auto p = producer(use.operand);
    if (!p) continue;
    if (owner && fields[owner->value] == p && use.role == OperandRole::kBase)
      continue;
    if (external[p->value]) escaped[p->value] = 1;
    external[p->value] = UseId{static_cast<std::uint32_t>(u)};
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const OpId call{static_cast<std::uint32_t>(i)};
    const auto property = MemberProperty(graph, plan, call);
    if (!property) continue;
    const auto placement = BeforeLogicalFusion(graph, plan, property->value);
    const bool proposed =
        placement.kind == EvaluationKind::kInline &&
        placement.consumer == graph.ops[call.value].first_use &&
        std::binary_search(plan.phase_compositions.begin(),
                           plan.phase_compositions.end(), call);
    if (!proposed &&
        (placement.kind != EvaluationKind::kRoot ||
         placement.temporary != TemporaryReason::kCrossRegionValue))
      continue;
    const auto& op = graph.ops[i];
    for (std::uint32_t j = 0; j < op.use_count; ++j) {
      budget.Step();
      const UseId u{op.first_use.value + j};
      const auto& use = graph.uses[u.value];
      if (use.role != OperandRole::kArgument) continue;
      const auto p = producer(use.operand);
      if (p && !escaped[p->value] && external[p->value] == u)
        seeded[p->value] = 1;
    }
  }
  bool any = false;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (!fields[i] || !seeded[fields[i]->value]) continue;
    const auto& op = graph.ops[i];
    for (std::uint32_t j = 0; j < op.use_count; ++j) {
      budget.Step();
      const auto& use = graph.uses[op.first_use.value + j];
      if (use.role != OperandRole::kStoredValue) continue;
      const auto v = ResolveEvaluationOperand(plan, use.operand);
      if (const auto* id = std::get_if<ValueId>(&v);
          id && std::holds_alternative<PhiDefinition>(
                    graph.values[id->value].origin)) {
        selected[id->value] = 1;
        any = true;
      }
    }
  }
  if (!any) return result;
  LiteralArmIndex proof(graph, plan, budget);
  budget.Array(result, graph.ops.size());
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step();
    const auto& use = graph.uses[u];
    const auto* merged = std::get_if<ValueId>(&use.owner);
    if (!merged || !selected[merged->value] ||
        use.role != OperandRole::kPhiInput)
      continue;
    const auto p = producer(use.operand);
    if (!p) continue;
    const auto entry = entries[graph.ops[p->value].block.value];
    if (!entry) continue;
    const auto checked =
        proof.Prove(*entry, UseId{static_cast<std::uint32_t>(u)});
    if (std::holds_alternative<LiteralArmView>(checked))
      result[p->value] = UseId{static_cast<std::uint32_t>(u)};
  }
  bool complete = false;
  for (const auto& input : result) {
    budget.Step();
    complete |= input.has_value();
  }
  if (!complete) result = {};
  return result;
}
}  // namespace abcd::source
