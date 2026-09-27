#include "phase_corridor.h"

#include <type_traits>

namespace abcd::source {
namespace {
using namespace analysis;
bool SameOperand(const ValueOperand& a, const ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, SpecialValue>) return value == std::get<T>(b);
    else return value.value == std::get<T>(b).value;
  }, a);
}
}  // namespace
void PhaseBudget::Step(std::size_t count) {
  if (count > remaining_.work) throw EvaluationError::kWorkLimit;
  remaining_.work -= count;
}
bool PhaseCorridors::Expression(const ValueOperation& operation) {
  // Explicit scalar/reference/call operands only. Environment, creation, module
  // guards, argument snapshots and initializer protocols retain their own fences.
  return std::holds_alternative<ValueCopy>(operation) ||
      std::holds_alternative<ValueString>(operation) ||
      std::holds_alternative<ValueUnary>(operation) ||
      std::holds_alternative<ValueBinary>(operation) ||
      std::holds_alternative<ValueNamedRead>(operation) ||
      std::holds_alternative<ValuePropertyRead>(operation) ||
      std::holds_alternative<ValueInvoke>(operation) ||
      std::holds_alternative<ValueConstruct>(operation);
}
bool PhaseCorridors::Event(const ValueOp& op, const OpEvaluation& placement) {
  return op.life == ValueLife::kLive && placement.kind != EvaluationKind::kAlias &&
      placement.kind != EvaluationKind::kElidedPure && OrderedEvaluation(op);
}
PhaseCorridors::PhaseCorridors(const ValueProgram& graph, const EvaluationPlan& plan, PhaseBudget& budget)
    : graph_(graph), plan_(plan), budget_(budget) {
  std::vector<std::size_t> incoming;
  std::vector<std::optional<EdgeId>> next;
  std::vector<std::uint8_t> previous, copied;
  budget.Array(incoming, graph.blocks.size()); budget.Array(next, graph.blocks.size());
  budget.Array(previous, graph.blocks.size()); budget.Array(positions_, graph.blocks.size());
  budget.Array(copied, graph.edges.size()); budget.Array(uses_, graph.values.size());
  budget.Array(exits_, graph.ops.size()); budget.Array(throws_, graph.ops.size());
  budget.Array(staged_, plan.exceptions ? plan.exceptions->slots.size() : 0);
  for (const auto& edge : graph.edges) { budget.Step(); ++incoming[edge.target.value]; }
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); const auto& block = graph.blocks[b];
    std::optional<EdgeId> edge;
    std::size_t count = 0;
    for (std::size_t e = block.first_edge.value; e < std::size_t(block.first_edge.value) + block.edge_count; ++e) {
      budget.Step(); if (graph.edges[e].kind == FlowEdgeKind::kException) continue;
      ++count; edge = EdgeId{static_cast<std::uint32_t>(e)};
    }
    if (count != 1 || graph.phases[b].phase == ExceptionPhase::kCatch) continue;
    const auto& flow = graph.edges[edge->value];
    if (flow.kind != FlowEdgeKind::kFallthrough || incoming[flow.target.value] != 1 ||
        graph.phases[flow.target.value].phase == ExceptionPhase::kCatch) continue;
    next[b] = edge; previous[flow.target.value] = 1;
  }
  if (plan.exceptions) {
    for (std::size_t b = 0; b < plan.phis->bundles.size(); ++b) {
      budget.Step(); const auto& placement = plan.exceptions->placements[b];
      if (const auto* normal = std::get_if<NormalCopies>(&placement))
        copied[normal->edge.value] = plan.phis->bundles[b].count != 0;
      if (const auto* raised = std::get_if<ThrowCopies>(&placement)) throws_[raised->operation.value] = b;
    }
  }
  // Roots have no eligible predecessor. A closed cycle stays unindexed.
  for (std::size_t root = 0; root < graph.blocks.size(); ++root) {
    budget.Step(); if (previous[root]) continue;
    PhasePosition pos{WorkBlockId{static_cast<std::uint32_t>(root)}};
    auto b = root;
    for (;;) {
      budget.Step();
      if (positions_[b].head) throw EvaluationError::kInvalidGraph;
      positions_[b] = pos;
      const auto& block = graph.blocks[b];
      for (std::size_t o = block.first_op.value; o < std::size_t(block.first_op.value) + block.op_count; ++o) {
        budget.Step(); pos.effects += Event(graph.ops[o], plan.operations[o]);
      }
      if (!next[b]) break;
      pos.copies += copied[next[b]->value]; ++pos.order;
      b = graph.edges[next[b]->value].target.value;
    }
  }
  for (std::size_t i = 0; i < graph.exceptional_exits.size(); ++i) {
    budget.Step(); exits_[graph.exceptional_exits[i].op.value] = i;
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step(); const auto& use = graph.uses[i];
    if (const auto* owner = std::get_if<OpId>(&use.owner)) {
      const auto kind = plan.operations[owner->value].kind;
      if (graph.ops[owner->value].life != ValueLife::kLive ||
          kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure) continue;
    }
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value) continue;
    auto& occurrence = uses_[value->value];
    if (occurrence.first) occurrence.multiple = true;
    else occurrence.first = UseId{static_cast<std::uint32_t>(i)};
  }
}
std::size_t PhaseCorridors::EffectsBefore(OpId op) const {
  return positions_[graph_.ops[op.value].block.value].effects;
}
bool PhaseCorridors::SameCatch(OpId producer, OpId consumer) {
  if (!exits_[producer.value] || !exits_[consumer.value] ||
      graph_.exceptional_exits[*exits_[producer.value]].handler !=
      graph_.exceptional_exits[*exits_[consumer.value]].handler) return false;
  const auto left = throws_[producer.value], right = throws_[consumer.value];
  const auto count = [&](std::optional<std::size_t> bundle) {
    return bundle ? plan_.phis->bundles[*bundle].count : 0U;
  };
  if (count(left) != count(right)) return false;
  if (!count(left)) return true;
  const auto& a = plan_.phis->bundles[*left];
  const auto& b = plan_.phis->bundles[*right];
  for (std::size_t c = a.first; c < std::size_t(a.first) + a.count; ++c) {
    budget_.Step(); staged_[plan_.exceptions->staging[c]->value] = c;
  }
  for (std::size_t c = b.first; c < std::size_t(b.first) + b.count; ++c) {
    budget_.Step(); const auto prior = staged_[plan_.exceptions->staging[c]->value];
    if (!prior || *prior < a.first || *prior >= std::size_t(a.first) + a.count ||
        !SameOperand(ResolveEvaluationOperand(plan_, plan_.phis->copies[*prior].source),
                     ResolveEvaluationOperand(plan_, plan_.phis->copies[c].source))) return false;
  }
  return true;
}
bool PhaseCorridors::CanFuse(OpId producer, OpId consumer, UseId use) const {
  const auto& child = graph_.ops[producer.value];
  const auto& parent = graph_.ops[consumer.value];
  if (producer.value >= consumer.value || child.life != ValueLife::kLive ||
      !child.result || !parent.result || !Expression(child.operation) || !Expression(parent.operation)) return false;
  const auto& uses = uses_[child.result->value];
  if (uses.multiple || uses.first != use) return false;
  // First phi/catch snapshots must remain stored. Their raw bindings are not
  // interchangeable with a later read, even when a copy has no language effect.
  if (std::holds_alternative<ValueCopy>(child.operation) &&
      std::holds_alternative<ValueId>(graph_.uses[child.first_use.value].operand)) return false;
  const auto& start = positions_[child.block.value];
  const auto& end = positions_[parent.block.value];
  if (!start.head || start.head != end.head || start.order >= end.order || start.copies != end.copies ||
      graph_.blocks[child.block.value].op_count != 1 || graph_.blocks[parent.block.value].op_count != 1) return false;
  return true;
}
}  // namespace abcd::source
