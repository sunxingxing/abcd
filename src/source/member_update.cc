#include "member_update.h"

#include "assignment_values.h"
#include "call_plan.h"
#include "effect_order.h"
#include "evaluation_aliases.h"
#include "logical_evaluation.h"
#include "phase_corridor.h"
#include "tree_checks.h"
#include "recovery_scratch.h"
namespace abcd::source {
namespace {
using namespace analysis;
constexpr std::size_t kReferenceObservations = 2;  // Original Get and Set.
constexpr std::size_t kMemberInputs = 3;           // Base, key, RHS.
constexpr std::size_t kMemberPorts = 4;  // Get, ToNumeric, operation, Set.
void Require(bool ok,
             MemberUpdateError error = MemberUpdateError::kCorrespondence) {
  if (!ok) throw error;
}
}  // namespace
bool MemberCompoundOperator(BinaryOperator op) {
  switch (op) {
    case BinaryOperator::kAdd:
    case BinaryOperator::kSubtract:
    case BinaryOperator::kMultiply:
    case BinaryOperator::kDivide:
    case BinaryOperator::kRemainder:
    case BinaryOperator::kExponent:
    case BinaryOperator::kShiftLeft:
    case BinaryOperator::kShiftRightUnsigned:
    case BinaryOperator::kShiftRightSigned:
    case BinaryOperator::kBitAnd:
    case BinaryOperator::kBitOr:
    case BinaryOperator::kBitXor:
      return true;
    default:
      return false;
  }
}
namespace {
void Inputs(const ValueProgram& graph, OpId id, TreeCheckBudget& budget) {
  const auto& op = graph.ops[id.value];
  const auto shape = DescribeEvaluation(op.operation);
  Require(shape && shape->Count() == op.use_count &&
              op.first_use.value <= graph.uses.size() &&
              op.use_count <= graph.uses.size() - op.first_use.value,
          MemberUpdateError::kUse);
  for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
    budget.Step();
    const auto& use = graph.uses[op.first_use.value + ordinal];
    Require(use.life == ValueLife::kLive &&
                use.owner == decltype(use.owner){id} &&
                !CheckEvaluationUse(graph, use, *shape, ordinal),
            MemberUpdateError::kUse);
  }
}
std::optional<OpId> Producer(const ValueProgram& g, const EvaluationPlan& p,
                             UseId u) {
  const auto input = ResolveEvaluationOperand(p, g.uses[u.value].operand);
  const auto* v = std::get_if<ValueId>(&input);
  const auto* result =
      v ? std::get_if<OpResult>(&g.values[v->value].origin) : nullptr;
  return result ? std::optional{result->op} : std::nullopt;
}
bool Same(const ValueProgram& g, const EvaluationPlan& p, UseId a, UseId b) {
  const auto x = ResolveEvaluationOperand(p, g.uses[a.value].operand),
             y = ResolveEvaluationOperand(p, g.uses[b.value].operand);
  if (x.index() != y.index()) return false;
  return std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SpecialValue>)
          return value == std::get<T>(y);
        else
          return value.value == std::get<T>(y).value;
      },
      x);
}
// Allocation-free semantic census. Unmet calls alone do not justify per-value
// indexes or a certificate arena: most families contain no member mutation.
// The same identity derivation runs in discovery and independent verification.
struct ReferenceCandidate {
  OpId read, calculation;
  std::optional<OpId> conversion;
  NativeMemberOperation operation;
  bool computed;
};
std::optional<ReferenceCandidate> Candidate(const ValueProgram& g,
    const EvaluationPlan& p, OpId id, TreeCheckBudget& budget) {
  const auto& write = g.ops[id.value];
  if (write.life != ValueLife::kLive) return {};
  const bool named = std::holds_alternative<ValueNamedWrite>(write.operation),
             computed =
                 std::holds_alternative<ValuePropertyWrite>(write.operation);
  if (!named && !computed) return {};
  Inputs(g, id, budget);
  const UseId stored{write.first_use.value + write.use_count - 1};
  const auto calc = Producer(g, p, stored);
  if (!calc) return {};
  const auto& operation = g.ops[calc->value];
  const auto* inc = std::get_if<ValueUnary>(&operation.operation);
  const auto* binary = std::get_if<ValueBinary>(&operation.operation);
  NativeMemberOperation native;
  if (inc && (inc->op == UnaryOperator::kIncrement ||
              inc->op == UnaryOperator::kDecrement))
    native = MemberIncrement{inc->op == UnaryOperator::kIncrement
                                 ? UpdateDirection::kIncrement
                                 : UpdateDirection::kDecrement,
                             UpdateResult::kPrefix};
  else if (binary && MemberCompoundOperator(binary->op))
    native = binary->op;
  else
    return {};
  // Reject non-arithmetic producers before visiting their operands. One call
  // result can feed many writes; revisiting its argument list per write is
  // quadratic in an otherwise linear-size graph.
  Inputs(g, *calc, budget);
  auto read = Producer(g, p, operation.first_use);
  std::optional<OpId> conversion;
  if (read)
    if (const auto* numeric =
            std::get_if<ValueUnary>(&g.ops[read->value].operation);
        numeric && numeric->op == UnaryOperator::kToNumeric) {
      Inputs(g, *read, budget);
      conversion = read;
      read = Producer(g, p, g.ops[read->value].first_use);
    }
  if (!read || (binary && conversion)) return {};
  const auto& get = g.ops[read->value];
  if (named) {
    const auto* property = std::get_if<ValueNamedRead>(&get.operation);
    if (!property || property->lookup != PropertyLookup::kObject ||
        property->name != std::get<ValueNamedWrite>(write.operation).name)
      return {};
  } else if (!std::holds_alternative<ValuePropertyRead>(get.operation))
    return {};
  Inputs(g, *read, budget);
  const auto base = get.first_use;
  if (!Same(g, p, base, write.first_use) ||
      (computed &&
       !Same(g, p, {base.value + 1}, {write.first_use.value + 1})))
    return {};
  return ReferenceCandidate{*read, *calc, conversion, native, computed};
}
}  // namespace
const MemberUpdatePlan& MemberUpdates(const EvaluationPlan& p) {
  static const MemberUpdatePlan empty;
  return p.member_updates ? *p.member_updates : empty;
}
const MemberUpdate* MemberUpdateAt(const EvaluationPlan& p, OpId id) {
  const auto& list = MemberUpdates(p).updates;
  const auto at =
      std::lower_bound(list.begin(), list.end(), id,
                       [](const auto& x, OpId n) { return x.write < n; });
  return at != list.end() && at->write == id ? &*at : nullptr;
}
bool MemberUpdateIntrinsic(const EvaluationPlan& p, OpId id) {
  return id.value < MemberUpdates(p).events.size() &&
         MemberUpdates(p).events[id.value] &&
         MemberUpdates(p).events[id.value] != id;
}
bool MemberUpdateWire(const EvaluationPlan& p, UseId id) {
  return id.value < MemberUpdates(p).wires.size() &&
         MemberUpdates(p).wires[id.value];
}
MemberUpdatePlan DiscoverMemberUpdates(const ValueProgram& g,
                                       const EvaluationPlan& p,
                                       TreeCheckBudget& budget) {
  MemberUpdatePlan out;
  bool needed = !MemberUpdates(p).updates.empty();
  for (std::size_t i = 0; i < g.ops.size() && !needed; ++i) {
    budget.Step();
    if (const auto property =
            MemberProperty(g, p, OpId{static_cast<std::uint32_t>(i)})) {
      const auto at = BeforeLogicalFusion(g, p, property->value);
      needed = at.kind == EvaluationKind::kRoot;
    }
  }
  if (!needed) return out;
  std::size_t candidates = 0;
  for (std::size_t i = 0; i < g.ops.size(); ++i) {
    budget.Step();
    candidates += Candidate(g, p, OpId{static_cast<std::uint32_t>(i)}, budget).has_value();
  }
  if (!candidates) return out;
  // No successful Reference is a seed. The same original disposition is
  // recovered after composition by its separately verified root registration.
  std::vector<std::uint8_t> proposed_calls;
  if (!MemberUpdates(p).updates.empty()) {
    budget.Array(proposed_calls, g.ops.size());
    for (const auto& update : MemberUpdates(p).updates) {
      budget.Step();
      Require(update.call.value < g.ops.size());
      proposed_calls[update.call.value] = 1;
    }
  }
  std::vector<std::optional<OpId>> calls, callees;
  std::vector<std::optional<UseId>> arguments;
  budget.Array(calls, g.values.size());
  budget.Array(callees, g.values.size());
  budget.Array(arguments, g.values.size());
  for (std::size_t i = 0; i < g.ops.size(); ++i) {
    budget.Step();
    const OpId call{static_cast<std::uint32_t>(i)};
    const auto property = MemberProperty(g, p, call);
    if (!property) continue;
    const auto placement = BeforeLogicalFusion(g, p, property->value);
    const bool proposed = placement.kind == EvaluationKind::kInline &&
                          placement.consumer == g.ops[i].first_use &&
                          (!proposed_calls.empty() && proposed_calls[i]);
    if (!proposed && placement.kind != EvaluationKind::kRoot) continue;
    for (std::uint32_t j = 0; j < g.ops[i].use_count; ++j) {
      budget.Step();
      const UseId u{g.ops[i].first_use.value + j};
      if (g.uses[u.value].role != OperandRole::kArgument) continue;
      const auto value = ResolveEvaluationOperand(p, g.uses[u.value].operand);
      if (const auto* v = std::get_if<ValueId>(&value)) {
        if (arguments[v->value]) {
          calls[v->value].reset();
          continue;
        }
        calls[v->value] = call;
        callees[v->value] = property;
        arguments[v->value] = u;
      }
    }
  }
  std::vector<std::uint32_t> counts;
  budget.Array(counts, g.values.size());
  for (std::size_t u = 0; u < g.uses.size(); ++u) {
    budget.Step();
    const auto& use = g.uses[u];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(p, use)) continue;
    const auto* op = std::get_if<OpId>(&use.owner);
    if (op && (p.operations[op->value].kind == EvaluationKind::kAlias ||
               p.operations[op->value].kind == EvaluationKind::kElidedPure))
      continue;
    const auto x = ResolveEvaluationOperand(p, use.operand);
    if (const auto* v = std::get_if<ValueId>(&x)) {
      ++counts[v->value];
    }
  }
  budget.Admit(candidates, sizeof(MemberUpdate));
  out.updates.reserve(candidates);
  budget.Admit(out.updates.capacity() - candidates, sizeof(MemberUpdate));
  for (std::size_t w = 0; w < g.ops.size(); ++w) {
    budget.Step();
    const auto candidate = Candidate(g, p, OpId{static_cast<std::uint32_t>(w)}, budget);
    if (!candidate) continue;
    const auto& write = g.ops[w];
    const auto read = std::optional{candidate->read};
    const auto calc = std::optional{candidate->calculation};
    const auto conversion = candidate->conversion;
    auto native = candidate->operation;
    const bool computed = candidate->computed;
    const auto& operation = g.ops[calc->value];
    const auto& get = g.ops[read->value];
    const auto base = get.first_use;
    const bool inc = std::holds_alternative<MemberIncrement>(native);
    const bool binary = !inc;
    if (!operation.result) continue;
    auto result = *operation.result;
    if (inc && conversion && g.ops[conversion->value].result &&
        calls[g.ops[conversion->value].result->value]) {
      result = *g.ops[conversion->value].result;
      std::get<MemberIncrement>(native).result = UpdateResult::kPostfix;
    }
    if (!calls[result.value] || !callees[result.value] ||
        !arguments[result.value])
      continue;
    // Internal read/numeric values have no foreign observations. The sole
    // external result is the original argument occurrence; the store wire
    // and postfix old-number dependency are explicit, never duplicate reads.
    if (!get.result || counts[get.result->value] != 1) continue;
    const auto result_count = counts[result.value];
    if (result_count != kReferenceObservations) continue;
    if (conversion && result != *g.ops[conversion->value].result &&
        counts[g.ops[conversion->value].result->value] != 1)
      continue;
    if (result != *operation.result && counts[operation.result->value] != 1)
      continue;
    MemberUpdate proof{
        *read,
        *calc,
        OpId{static_cast<std::uint32_t>(w)},
        *calls[result.value],
        *callees[result.value],
        *read,
        base,
        computed ? std::optional{UseId{base.value + 1}} : std::nullopt,
        binary ? std::optional{UseId{operation.first_use.value + 1}}
               : std::nullopt,
        arguments[result.value],
        conversion,
        native,
        result,
        {},
        {}};
    budget.Admit(kMemberInputs, sizeof(MemberUpdateInput));
    proof.inputs.reserve(kMemberInputs);
    budget.Admit(proof.inputs.capacity() - kMemberInputs,
                 sizeof(MemberUpdateInput));
    for (const auto input : {std::optional{proof.base}, proof.key, proof.rhs})
      if (input) {
        budget.Step();
        if (const auto producer = Producer(g, p, *input)) {
          const auto value =
              ResolveEvaluationOperand(p, g.uses[input->value].operand);
          const auto id = std::get<ValueId>(value);
          const bool shared =
              input != proof.rhs && counts[id.value] == kReferenceObservations;
          const auto placement = BeforeLogicalFusion(g, p, producer->value);
          const bool eager = placement.kind == EvaluationKind::kInline &&
                             placement.consumer == input;
          if (shared || eager)
            proof.inputs.push_back({*input, *producer, shared});
        }
      }
    out.updates.push_back(std::move(proof));
  }
  if (out.updates.empty()) return {};
  budget.Array(out.results, g.uses.size());
  budget.Array(out.events, g.ops.size());
  budget.Array(out.moves, g.ops.size());
  budget.Array(out.wires, g.uses.size());
  budget.Array(out.owned, g.ops.size());
  // One prepaid functional-owner index contracts the existing inline forests.
  // Reference inputs and intrinsic ports cut that forest. The shared effect
  // equations then prove a must-reach chain, not adjacency or block layout.
  std::vector<OpId> parents, roots, stack;
  std::vector<std::uint8_t> color;
  std::vector<ExecutionAnchor> anchors;
  budget.Array(parents, g.ops.size());
  budget.Array(roots, g.ops.size());
  budget.Array(stack, g.ops.size());
  budget.Array(color, g.ops.size());
  budget.Array(anchors, g.ops.size());
  for (std::size_t i = 0; i < g.ops.size(); ++i) {
    budget.Step();
    parents[i] = OpId{static_cast<std::uint32_t>(i)};
    const auto placement = BeforeLogicalFusion(g, p, i);
    if (placement.kind == EvaluationKind::kInline && placement.consumer)
      if (const auto* op =
              std::get_if<OpId>(&g.uses[placement.consumer->value].owner))
        parents[i] = *op;
  }
  for (const auto& module : p.modules) {
    budget.Step();
    if (module.pair.guard) parents[module.pair.guard->value] = module.pair.read;
  }
  for (const auto& mutation : out.updates) {
    for (const auto id : {mutation.read, mutation.calculation, mutation.write,
                          mutation.callee, mutation.call}) {
      parents[id.value] = id;
      anchors[id.value] = ExecutionAnchor::kRequired;
    }
    if (mutation.conversion) {
      parents[mutation.conversion->value] = *mutation.conversion;
      anchors[mutation.conversion->value] = ExecutionAnchor::kRequired;
    }
    for (const auto& input : mutation.inputs) {
      budget.Step();
      parents[input.producer.value] = input.producer;
      anchors[input.producer.value] = ExecutionAnchor::kRequired;
    }
  }
  for (std::size_t i = 0; i < g.ops.size(); ++i) {
    budget.Step();
    if (color[i]) continue;
    std::size_t depth = 0, at = i;
    while (!color[at]) {
      budget.Step();
      color[at] = 1;
      stack[depth++] = {static_cast<std::uint32_t>(at)};
      if (parents[at].value == at) break;
      at = parents[at].value;
    }
    Require(parents[at].value == at || color[at] == 2);
    const auto root = parents[at].value == at
                          ? OpId{static_cast<std::uint32_t>(at)}
                          : roots[at];
    while (depth) {
      budget.Step();
      const auto id = stack[--depth];
      roots[id.value] = root;
      color[id.value] = 2;
    }
  }
  auto order_result = BuildEffectOrder(g, budget.Remaining(), roots, anchors);
  if (const auto* e = std::get_if<ValueAvailabilityError>(&order_result)) {
    if (*e == ValueAvailabilityError::kResourceLimit)
      throw TreeStructureError::kResourceLimit;
    if (*e == ValueAvailabilityError::kWorkLimit)
      throw TreeStructureError::kWorkLimit;
    throw MemberUpdateError::kOrder;
  }
  const auto& order = std::get<EffectOrder>(order_result);
  budget.Admit(order.allocated_bytes, 1);
  budget.Step(order.work);
  std::vector<std::optional<std::size_t>> exceptions, by_root;
  budget.Array(exceptions, g.ops.size());
  budget.Array(by_root, g.ops.size());
  for (std::size_t i = 0; i < g.exceptional_exits.size(); ++i) {
    budget.Step();
    Require(!exceptions[g.exceptional_exits[i].op.value],
            MemberUpdateError::kCatchState);
    exceptions[g.exceptional_exits[i].op.value] = i;
  }
  std::size_t kept = 0;
  for (auto& mutation : out.updates) {
    std::array<OpId, kMemberInputs + kMemberPorts> chain{};
    std::size_t count = 0;
    for (const auto& input : mutation.inputs)
      if (input.use == mutation.base || input.use == mutation.key)
        chain[count++] = input.producer;
    chain[count++] = mutation.read;
    if (mutation.conversion) chain[count++] = *mutation.conversion;
    for (const auto& input : mutation.inputs)
      if (input.use == mutation.rhs) chain[count++] = input.producer;
    chain[count++] = mutation.calculation;
    chain[count++] = mutation.write;
    bool valid = true;
    for (std::size_t i = 1; i < count; ++i) {
      budget.Step();
      valid &= order.after[chain[i - 1].value] == chain[i] &&
               order.before[chain[i].value] == EffectPredecessor{chain[i - 1]};
    }
    if (!valid) continue;
    mutation.first = chain[0];
    if (p.flow == EvaluationFlow::kNormal &&
        (order.after[mutation.callee.value] != mutation.first ||
         order.after[mutation.write.value] != mutation.call))
      continue;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step();
      by_root[chain[i].value] = kept;
    }
    budget.Admit(kMemberPorts, sizeof(MemberUpdatePort));
    mutation.ports.reserve(kMemberPorts);
    budget.Admit(mutation.ports.capacity() - kMemberPorts,
                 sizeof(MemberUpdatePort));
    const auto add = [&](OpId id, MemberUpdatePhase phase) {
      MemberUpdatePort port{id,
                            phase,
                            order.completions.empty()
                                ? std::nullopt
                                : order.completions[id.value],
                            exceptions[id.value],
                            {}};
      if (port.exception)
        port.handler = g.exceptional_exits[*port.exception].handler;
      mutation.ports.push_back(port);
    };
    add(mutation.read, MemberUpdatePhase::kGet);
    if (mutation.conversion)
      add(*mutation.conversion, MemberUpdatePhase::kToNumeric);
    add(mutation.calculation, MemberUpdatePhase::kOperation);
    add(mutation.write, MemberUpdatePhase::kSet);
    if (kept != std::size_t(&mutation - out.updates.data()))
      out.updates[kept] = std::move(mutation);
    ++kept;
  }
  out.updates.resize(kept);
  if (!kept) return {};
  std::vector<std::uint8_t> bad;
  budget.Array(bad, kept);
  if (p.flow == EvaluationFlow::kPhased) {
    PhaseBudget phase_budget(budget.Remaining());
    PhaseCorridors corridors(g, p, phase_budget);
    budget.Admit(phase_budget.Cost().bytes, 1);
    std::uint64_t accounted = phase_budget.Cost().work;
    budget.Step(accounted);
    for (std::size_t i = 0; i < g.ops.size(); ++i) {
      budget.Step();
      if (!by_root[roots[i].value]) continue;
      const auto candidate = *by_root[roots[i].value];
      const auto& update = out.updates[candidate];
      if (g.ops[i].life == ValueLife::kLive &&
          (HasOrderedEffect(g.ops[i]) ||
           anchors[i] == ExecutionAnchor::kRequired)) {
        bad[candidate] |=
            corridors.HasCatchCopies({static_cast<std::uint32_t>(i)}) ||
            (g.ops[i].exceptional == ExceptionalCompletion::kMayThrow &&
             !corridors.SameCatch({static_cast<std::uint32_t>(i)},
                                  update.write));
        budget.Step(phase_budget.Cost().work - accounted);
        accounted = phase_budget.Cost().work;
      }
    }
  }
  std::vector<std::optional<OpId>> root_write;
  budget.Array(root_write, g.ops.size());
  kept = 0;
  for (std::size_t i = 0; i < out.updates.size(); ++i) {
    budget.Step();
    auto& update = out.updates[i];
    if (bad[i]) continue;
    for (const auto& port : update.ports) {
      Require(!out.events[port.operation.value]);
      out.events[port.operation.value] = update.write;
    }
    Require(update.result.has_value());
    Require(!out.results[update.result->value]);
    out.results[update.result->value] = update.write;
    const auto& write = g.ops[update.write.value];
    out.wires[write.first_use.value] = 1;
    if (update.key) out.wires[write.first_use.value + 1] = 1;
    for (const auto& input : update.inputs)
      if (input.moved) out.moves[input.producer.value] = input.use;
    for (const auto& port : update.ports)
      root_write[port.operation.value] = update.write;
    for (const auto& input : update.inputs)
      root_write[input.producer.value] = update.write;
    if (kept != i) out.updates[kept] = std::move(update);
    ++kept;
  }
  out.updates.resize(kept);
  if (!kept) return {};
  for (std::size_t i = 0; i < g.ops.size(); ++i) {
    budget.Step();
    out.owned[i] = root_write[roots[i].value];
  }
  return out;
}
void BuildMemberUpdates(const ValueProgram& g, EvaluationPlan& p,
                        TreeCheckBudget& budget) {
  auto discovered = DiscoverMemberUpdates(g, p, budget);
  p.member_updates.reset();
  if (discovered.updates.empty()) return;
  const auto before = budget.ActiveBytes();
  auto owned = std::allocate_shared<MemberUpdatePlan>(
      RecoveryAllocator<MemberUpdatePlan>{budget}, std::move(discovered));
  owned->allocated_bytes = budget.ActiveBytes() - before +
      owned->updates.capacity() * sizeof(MemberUpdate) +
      (owned->events.capacity() + owned->owned.capacity() + owned->results.capacity()) *
          sizeof(std::optional<OpId>) +
      owned->moves.capacity() * sizeof(std::optional<UseId>) + owned->wires.capacity();
  for (const auto& update : owned->updates) {
    budget.Step();
    owned->allocated_bytes += update.inputs.capacity() * sizeof(MemberUpdateInput) +
                              update.ports.capacity() * sizeof(MemberUpdatePort);
  }
  p.member_updates = std::move(owned);
  for (const auto& update : MemberUpdates(p).updates) {
    budget.Step();
    p.operations[update.read.value] = {
        EvaluationKind::kInline,
        g.ops[update.conversion.value_or(update.calculation).value].first_use,
        {}};
    if (update.conversion)
      p.operations[update.conversion->value] = {
          EvaluationKind::kInline,
          g.ops[update.calculation.value].first_use,
          {}};
    p.operations[update.calculation.value] = {
        EvaluationKind::kInline,
        UseId{g.ops[update.write.value].first_use.value +
              g.ops[update.write.value].use_count - 1},
        {}};
    p.operations[update.write.value] = {
        EvaluationKind::kInline, update.result, {}};
    if (p.flow == EvaluationFlow::kNormal)
      p.operations[update.callee.value] = {
          EvaluationKind::kInline, g.ops[update.call.value].first_use, {}};
  }
  for (std::size_t i = 0; i < MemberUpdates(p).moves.size(); ++i)
    if (MemberUpdates(p).moves[i]) {
      budget.Step();
      p.operations[i] = {
          EvaluationKind::kInline, MemberUpdates(p).moves[i], {}};
    }
}
void VerifyMemberUpdates(const ValueProgram& g, const EvaluationPlan& p,
                         TreeCheckBudget& budget) {
  if (MemberUpdates(p).updates.empty()) {
    Require(MemberUpdates(p).events.empty() &&
            MemberUpdates(p).results.empty() &&
            MemberUpdates(p).moves.empty() && MemberUpdates(p).wires.empty() &&
            MemberUpdates(p).owned.empty());
    return;
  }
  const auto expected = DiscoverMemberUpdates(g, p, budget);
  Require(expected.updates == MemberUpdates(p).updates &&
          expected.results == MemberUpdates(p).results &&
          expected.events == MemberUpdates(p).events &&
          expected.moves == MemberUpdates(p).moves &&
          expected.wires == MemberUpdates(p).wires &&
          expected.owned == MemberUpdates(p).owned);
  for (const auto& update : expected.updates) {
    budget.Step();
    Require(p.operations[update.read.value].kind == EvaluationKind::kInline &&
            p.operations[update.read.value].consumer ==
                g.ops[update.conversion.value_or(update.calculation).value]
                    .first_use);
    if (update.conversion)
      Require(p.operations[update.conversion->value].kind ==
                  EvaluationKind::kInline &&
              p.operations[update.conversion->value].consumer ==
                  g.ops[update.calculation.value].first_use);
    Require(p.operations[update.calculation.value].kind ==
                EvaluationKind::kInline &&
            p.operations[update.calculation.value].consumer ==
                UseId{g.ops[update.write.value].first_use.value +
                      g.ops[update.write.value].use_count - 1});
    Require(p.operations[update.write.value].kind == EvaluationKind::kInline &&
            p.operations[update.write.value].consumer == update.result);
  }
  for (std::size_t i = 0; i < expected.moves.size(); ++i)
    if (expected.moves[i])
      Require(p.operations[i].kind == EvaluationKind::kInline &&
              p.operations[i].consumer == expected.moves[i]);
}
}  // namespace abcd::source
