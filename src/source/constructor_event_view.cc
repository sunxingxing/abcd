#include "constructor_event_view.h"

#include "evaluation_aliases.h"
#include "literal_event_view.h"
#include "logical_evaluation.h"
namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value, ConstructorEventError reason) {
  if (!value) throw reason;
}
ConstructorEventView Derive(const ValueProgram& graph,
                            const EvaluationPlan& plan,
                            TreeCheckBudget& budget) {
  Require(plan.revision == graph.revision &&
              plan.cfg_revision == graph.cfg_revision,
          ConstructorEventError::kRevision);
  Require(plan.operations.size() == graph.ops.size(),
          ConstructorEventError::kCoverage);
  ConstructorEventView out{graph.revision, graph.cfg_revision, {}, {}, {}};
  std::vector<std::optional<OpId>> parent, root;
  std::vector<std::uint8_t> state, escaped;
  std::vector<OpId> stack;
  budget.Array(out.events, graph.ops.size());
  budget.Array(out.outputs, graph.ops.size());
  budget.Array(out.inputs, graph.uses.size());
  budget.Array(parent, graph.ops.size());
  budget.Array(root, graph.ops.size());
  budget.Array(state, graph.ops.size());
  budget.Array(escaped, graph.ops.size());
  budget.Array(stack, graph.ops.size());
  std::optional<LiteralEventView> literals;
  if (plan.literals)
    literals = BuildLiteralEventView(graph, *plan.literals, budget);
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    const auto& op = graph.ops[i];
    if (op.life != ValueLife::kLive) continue;
    const auto placement = BeforeLogicalFusion(graph, plan, i);
    if (literals && literals->events[i] &&
        literals->events[i]->allocation.value != i)
      parent[i] = literals->events[i]->allocation;
    else if ((placement.kind == EvaluationKind::kInline ||
              placement.kind == EvaluationKind::kNestedLiteral) &&
             placement.consumer) {
      Require(placement.consumer->value < graph.uses.size(),
              ConstructorEventError::kCorrespondence);
      const auto* owner =
          std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
      if (owner) {
        Require(owner->value < graph.ops.size(),
                ConstructorEventError::kCorrespondence);
        parent[i] = *owner;
      }
    }
  }
  // A verified module load's HoleGuard is part of that read, not a foreign
  // arm event. Initialized cell guards likewise retain their certified owner.
  for (const auto& module : plan.modules) {
    budget.Step();
    if (module.pair.guard) parent[module.pair.guard->value] = module.pair.read;
  }
  if (plan.cells)
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const OpId id{static_cast<std::uint32_t>(i)};
      if (!IsInitializedCellGuard(graph, *plan.cells, id)) continue;
      const auto& op = graph.ops[i];
      if (op.use_count != 1) continue;
      const auto input = ResolveEvaluationOperand(
          plan, graph.uses[op.first_use.value].operand);
      const auto* v = std::get_if<ValueId>(&input);
      const auto* producer =
          v ? std::get_if<OpResult>(&graph.values[v->value].origin) : nullptr;
      if (producer) parent[i] = producer->op;
    }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (state[i]) continue;
    std::size_t count = 0, at = i;
    while (!state[at]) {
      budget.Step();
      state[at] = 1;
      stack[count++] = {static_cast<std::uint32_t>(at)};
      if (!parent[at]) break;
      at = parent[at]->value;
    }
    Require(!parent[at] || state[at] == 2,
            ConstructorEventError::kCorrespondence);
    const auto owner =
        parent[at] ? root[at]
        : std::holds_alternative<ValueConstruct>(graph.ops[at].operation)
            ? std::optional{OpId{static_cast<std::uint32_t>(at)}}
            : std::nullopt;
    while (count) {
      budget.Step();
      const auto id = stack[--count];
      root[id.value] = owner;
      state[id.value] = 2;
    }
  }
  // Census every actual observation. A selected constructor result has one
  // phi-edge consumer; internal operands must stay in its original forest.
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step();
    const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
    if (const auto* owner = std::get_if<OpId>(&use.owner)) {
      if (graph.ops[owner->value].life != ValueLife::kLive ||
          plan.operations[owner->value].kind == EvaluationKind::kAlias ||
          plan.operations[owner->value].kind == EvaluationKind::kElidedPure)
        continue;
    }
    const auto input = ResolveEvaluationOperand(plan, use.operand);
    const auto* v = std::get_if<ValueId>(&input);
    const auto* producer =
        v ? std::get_if<OpResult>(&graph.values[v->value].origin) : nullptr;
    if (!producer || !root[producer->op.value]) continue;
    const auto owner = *root[producer->op.value];
    if (const auto* consuming = std::get_if<OpId>(&use.owner);
        consuming && root[consuming->value] == owner)
      continue;
    const UseId occurrence{static_cast<std::uint32_t>(u)};
    if (producer->op != owner || out.outputs[owner.value])
      escaped[owner.value] = 1;
    out.outputs[owner.value] = occurrence;
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (!root[i] || escaped[root[i]->value] || !out.outputs[root[i]->value])
      continue;
    const auto owner = *root[i];
    const auto output = *out.outputs[owner.value];
    const auto& occurrence = graph.uses[output.value];
    if (occurrence.role != OperandRole::kPhiInput ||
        !std::holds_alternative<ValueId>(occurrence.owner))
      continue;
    const auto& op = graph.ops[i];
    const auto placement = BeforeLogicalFusion(graph, plan, i);
    if (op.life != ValueLife::kLive ||
        placement.kind == EvaluationKind::kAlias ||
        placement.kind == EvaluationKind::kElidedPure)
      continue;
    const bool literal = literals && literals->events[i].has_value();
    const bool guard = placement.kind == EvaluationKind::kModuleGuard ||
                       placement.kind == EvaluationKind::kCellGuard;
    std::optional<WorkBlockId> commit;
    if (op.result)
      if (const auto* result =
              std::get_if<OpResult>(&graph.values[op.result->value].origin))
        commit = result->commit;
    out.events[i] = ConstructorEvent{
        OpId{static_cast<std::uint32_t>(i)},
        owner,
        i == owner.value ? ConstructorEventRole::kInvocation
        : literal        ? ConstructorEventRole::kLiteral
        : guard          ? ConstructorEventRole::kGuard
                         : ConstructorEventRole::kOperand,
        literal ? std::optional{literals->events[i]->allocation} : std::nullopt,
        placement.consumer,
        commit,
        {}};
  }
  for (std::size_t b = 0; b < graph.phases.size(); ++b) {
    budget.Step();
    const auto& phase = graph.phases[b];
    if (phase.operation && phase.phase == ExceptionPhase::kCommit &&
        out.events[phase.operation->value])
      out.events[phase.operation->value]->commit =
          WorkBlockId{static_cast<std::uint32_t>(b)};
  }
  for (std::size_t e = 0; e < graph.exceptional_exits.size(); ++e) {
    budget.Step();
    const auto& exit = graph.exceptional_exits[e];
    if (out.events[exit.op.value]) {
      out.events[exit.op.value]->exception = e;
      out.events[exit.op.value]->handler = exit.handler;
    }
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (!out.events[i]) continue;
    Require(out.events[i]->exception.has_value() ==
                (graph.ops[i].exceptional == ExceptionalCompletion::kMayThrow),
            ConstructorEventError::kCorrespondence);
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step();
    const auto& use = graph.uses[i];
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (use.life != ValueLife::kLive || !owner || !out.events[owner->value])
      continue;
    out.inputs[i] =
        ConstructorInput{UseId{static_cast<std::uint32_t>(i)},
                         out.events[owner->value]->constructor, use.role,
                         ResolveEvaluationOperand(plan, use.operand)};
  }
  return out;
}
}  // namespace
ConstructorEventView BuildConstructorEventView(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget) {
  return Derive(graph, plan, budget);
}
void VerifyConstructorEventView(const analysis::ValueProgram& graph,
                                const EvaluationPlan& plan,
                                const ConstructorEventView& supplied,
                                TreeCheckBudget& budget) {
  Require(supplied.revision == graph.revision &&
              supplied.cfg_revision == graph.cfg_revision,
          ConstructorEventError::kRevision);
  const auto expected = Derive(graph, plan, budget);
  Require(supplied.events == expected.events &&
              supplied.outputs == expected.outputs &&
              supplied.inputs.size() == expected.inputs.size(),
          ConstructorEventError::kCorrespondence);
  for (std::size_t i = 0; i < expected.inputs.size(); ++i) {
    budget.Step();
    const auto& a = supplied.inputs[i];
    const auto& b = expected.inputs[i];
    Require(a.has_value() == b.has_value(),
            ConstructorEventError::kCorrespondence);
    if (!b) continue;
    Require(a->occurrence == b->occurrence &&
                a->constructor == b->constructor && a->role == b->role &&
                a->value.index() == b->value.index(),
            ConstructorEventError::kCorrespondence);
    const bool same = std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, analysis::SpecialValue>)
            return value == std::get<T>(b->value);
          else
            return value.value == std::get<T>(b->value).value;
        },
        a->value);
    Require(same, ConstructorEventError::kCorrespondence);
  }
}
}  // namespace abcd::source
