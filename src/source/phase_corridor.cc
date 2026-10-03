#include "phase_corridor.h"

#include <type_traits>

namespace abcd::source {
namespace {
using namespace analysis;
constexpr std::size_t kGuardedReadEffects = 2;  // Original module load and TDZ guard.
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
  // Guarded modules are paired by the caller's independently checked module
  // index; their two original effects form one subtree, never a pure name read.
  return Aggregate(operation) || std::holds_alternative<ValueBranch>(operation) ||
      std::holds_alternative<ValueCopy>(operation) ||
      std::holds_alternative<ValueString>(operation) ||
      std::holds_alternative<ValueUnary>(operation) ||
      std::holds_alternative<ValueBinary>(operation) ||
      // Cell evaluation proves initialization; the read remains ordered so
      // callbacks, cell writes and catch snapshots still prevent relocation.
      std::holds_alternative<ValueLexicalLoad>(operation) ||
      std::holds_alternative<ValueModuleRead>(operation) ||
      std::holds_alternative<ValueGlobalRead>(operation) ||
      std::holds_alternative<ValueNamedRead>(operation) ||
      std::holds_alternative<ValuePropertyRead>(operation) ||
      std::holds_alternative<ValueIterResult>(operation) ||
      // Creation remains an ordered event. Fuse only a single-use callback
      // inside the same no-copy corridor, effect suffix and catch snapshot;
      // environment barriers and the final-root epoch check remain binding.
      std::holds_alternative<ValueFunctionCreation>(operation) ||
      std::holds_alternative<ValueInvoke>(operation) ||
      std::holds_alternative<ValueConstruct>(operation) ||
      std::holds_alternative<ValueConstructApply>(operation);
}
bool PhaseCorridors::Aggregate(const ValueOperation& operation) {
  return std::holds_alternative<ValueObjectAllocation>(operation) ||
      std::holds_alternative<ValueArrayAllocation>(operation);
}
bool PhaseCorridors::Event(const ValueOp& op, const OpEvaluation& placement) {
  return op.life == ValueLife::kLive && placement.kind != EvaluationKind::kAlias &&
      placement.kind != EvaluationKind::kElidedPure && placement.kind != EvaluationKind::kCellGuard && OrderedEvaluation(op);
}
PhaseCorridors::PhaseCorridors(const ValueProgram& graph, const EvaluationPlan& plan, PhaseBudget& budget,
    PhaseLiteralIntervals literals)
    : graph_(graph), plan_(plan), budget_(budget) {
  std::vector<std::size_t> incoming;
  std::vector<std::optional<EdgeId>> next;
  std::vector<std::uint8_t> previous, copied;
  budget.Array(incoming, graph.blocks.size()); budget.Array(next, graph.blocks.size());
  budget.Array(previous, graph.blocks.size()); budget.Array(positions_, graph.blocks.size());
  budget.Array(copied, graph.edges.size()); budget.Array(uses_, graph.values.size());
  budget.Array(exits_, graph.ops.size()); budget.Array(throws_, graph.ops.size());
  budget.Array(staged_, plan.exceptions ? plan.exceptions->slots.size() : 0);
  if (literals == PhaseLiteralIntervals::kInclude && plan.literals) {
    budget.Array(literal_ends_, graph.ops.size());
    // LiteralPlan already checked each complete laminar initializer interval.
    // Index once; native call arguments consume the entire creation subtree.
    for (const auto& literal : plan.literals->objects) {
      budget.Step(); if (literal.form != LiteralForm::kExpression) continue;
      // This index is also constructed while independently checking a proposed
      // LiteralPlan. Validate bounds before using any of its supplied IDs.
      if (literal.allocation.value >= graph.ops.size() || literal.count == 0 ||
          literal.first > plan.literals->definitions.size() ||
          literal.count > plan.literals->definitions.size() - literal.first)
        throw EvaluationError::kOwnership;
      const auto last = plan.literals->definitions[std::size_t(literal.first) + literal.count - 1];
      if (last.value >= graph.ops.size()) throw EvaluationError::kOwnership;
      literal_ends_[literal.allocation.value] = last;
    }
  }
  if (!plan.modules.empty()) {
    budget.Array(module_peers_, graph.ops.size());
    for (const auto& module : plan.modules) {
      budget.Step();
      // An initialized read has one unchanged event; only guarded pairs fuse.
      if (!module.pair.guard) continue;
      module_peers_[module.pair.read.value] = module.pair.guard;
      module_peers_[module.pair.guard->value] = module.pair.read;
    }
  }
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
          kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure || kind == EvaluationKind::kCellGuard) continue;
      // Only the independently paired guard dependency is absorbed. Catch
      // snapshots and every other canonical use still prevent duplication.
      if (kind == EvaluationKind::kModuleGuard && !module_peers_.empty() &&
          module_peers_[owner->value]) continue;
      if (IsMemberReceiver(graph, plan, use)) continue;
      // Owned definitions consume the allocation as their implicit literal
      // base, not as a second source value. LiteralPlan proved that ownership.
      if (kind == EvaluationKind::kInitializer && use.role == OperandRole::kBase &&
          (std::holds_alternative<ValueOwnWrite>(graph.ops[owner->value].operation) ||
           std::holds_alternative<ValueIndexedWrite>(graph.ops[owner->value].operation))) continue;
    }
    const auto operand = ResolveEvaluationOperand(plan, use.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value) continue;
    auto& occurrence = uses_[value->value];
    if (occurrence.first) occurrence.multiple = true;
    else occurrence.first = UseId{static_cast<std::uint32_t>(i)};
  }
}
bool IsMemberReceiver(const ValueProgram& graph, const EvaluationPlan& plan, const OperandUse& use) {
  // A native member call consumes its base once; the explicit VM receiver
  // supplies that same reference, not a second source evaluation. Mismatches
  // and other uses remain visible; CallPlan and TreeOperands check the syntax.
  const auto* owner = std::get_if<OpId>(&use.owner);
  if (!owner || use.role != OperandRole::kReceiver) return false;
  const auto& operation = graph.ops[owner->value];
  const auto* invoke = std::get_if<ValueInvoke>(&operation.operation);
  if (!invoke || invoke->receiver != InvokeReceiver::kExplicit) return false;
  const auto callee = ResolveEvaluationOperand(plan, graph.uses[operation.first_use.value].operand);
  const auto* id = std::get_if<ValueId>(&callee);
  const auto* result = id ? std::get_if<OpResult>(&graph.values[id->value].origin) : nullptr;
  if (!result) return false;
  const auto& property = graph.ops[result->op.value];
  return (std::holds_alternative<ValueNamedRead>(property.operation) ||
          std::holds_alternative<ValuePropertyRead>(property.operation)) &&
      SameOperand(ResolveEvaluationOperand(plan, graph.uses[property.first_use.value].operand),
                  ResolveEvaluationOperand(plan, use.operand));
}
std::size_t PhaseCorridors::EffectsBefore(OpId op) const {
  return positions_[graph_.ops[op.value].block.value].effects;
}
PhaseEffectInterval PhaseCorridors::Interval(OpId op) const {
  const auto last = !literal_ends_.empty() && literal_ends_[op.value] ? *literal_ends_[op.value] : op;
  const auto end = EffectsBefore(last) + EffectCount(last), begin = EffectsBefore(op);
  if (end < begin) throw EvaluationError::kOrder;
  return {end - begin, end};
}
std::size_t PhaseCorridors::EffectCount(OpId op) const {
  const bool event = Event(graph_.ops[op.value], plan_.operations[op.value]);
  if (!event) return 0;
  if (std::holds_alternative<ValueModuleRead>(graph_.ops[op.value].operation)) {
    // IndexModuleEvaluations proved adjacency through the read's empty Commit
    // phase; VerifyModuleCatchCopies also preserves both exception snapshots.
    // IndexModuleEvaluations covers every read. Initialized local reads keep
    // their single original event and do not consume a fictitious guard.
    if (module_peers_.empty() || !module_peers_[op.value]) return 1;
    return kGuardedReadEffects;
  }
  return 1;
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
bool PhaseCorridors::HasCatchCopies(OpId operation) const {
  const auto bundle = throws_[operation.value];
  return bundle && plan_.phis->bundles[*bundle].count != 0;
}
bool PhaseCorridors::CanFuse(OpId producer, OpId consumer, UseId use) const {
  const auto& child = graph_.ops[producer.value];
  const auto& parent = graph_.ops[consumer.value];
  if (producer.value >= consumer.value || child.life != ValueLife::kLive ||
      !child.result || (!parent.result && !std::holds_alternative<ValueBranch>(parent.operation)) ||
      !Expression(child.operation) || !Expression(parent.operation)) return false;
  const auto& uses = uses_[child.result->value];
  if (uses.multiple || uses.first != use) return false;
  // A pure language expression with only literal inputs has no binding state,
  // receiver identity or catch timing to preserve across a CFG split. Original
  // SSA availability still applies. Use shared evaluation semantics, never an
  // opcode list; mutable/phi/catch reads retain the corridor rules below.
  const auto shape = DescribeEvaluation(child.operation);
  if (shape && shape->expression && shape->inputs == InputEvaluation::kEager &&
      child.normal == LanguageEffect::kNone && child.before_throw == LanguageEffect::kNone &&
      child.exceptional == ExceptionalCompletion::kImpossible &&
      graph_.values[child.result->value].domain.IsLanguage()) {
    bool invariant = true;
    for (std::uint32_t i = 0; i < child.use_count; ++i) {
      budget_.Step(); invariant &= !std::holds_alternative<ValueId>(graph_.uses[child.first_use.value + i].operand);
    }
    if (invariant) return true;
  }
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
