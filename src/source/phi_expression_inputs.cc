#include "evaluation_plan.h"
#include "phi_copies.h"
#include "tree_checks.h"
#include "reference_arguments.h"
#include "../analysis/value_flow.h"

#include <algorithm>
#include <limits>

namespace abcd::source {
namespace {
using namespace analysis;
struct PhiObservation {
  std::optional<UseId> first;
  bool multiple = false;
};
bool Inert(const ValueOp& op) {
  if (op.life != ValueLife::kLive) return true;
  if (op.normal != LanguageEffect::kNone ||
      op.before_throw != LanguageEffect::kNone ||
      op.exceptional != ExceptionalCompletion::kImpossible ||
      IsEnvironmentState(op.operation))
    return false;
  const auto shape = DescribeEvaluation(op.operation);
  return !OrderedEvaluation(op) ||
         (shape && shape->control == ControlEvaluation::kNormal);
}
class PhiTails {
 public:
  PhiTails(const ValueProgram& graph, const PhiCopies& copies,
           TreeCheckBudget& budget, std::span<const std::uint8_t> guards = {})
      : graph_(graph), budget_(budget) {
    budget.Array(copied_, graph.edges.size());
    budget.Array(last_, graph.blocks.size());
    budget.Array(endpoints_, graph.blocks.size());
    std::vector<std::uint8_t> inert, failed, queued;
    std::vector<std::uint32_t> pending;
    std::vector<std::optional<EdgeId>> heads, next;
    std::vector<WorkBlockId> queue;
    budget.Array(inert, graph.blocks.size());
    budget.Array(failed, graph.blocks.size());
    budget.Array(queued, graph.blocks.size());
    budget.Array(pending, graph.blocks.size());
    budget.Array(heads, graph.blocks.size());
    budget.Array(next, graph.edges.size());
    budget.Array(queue, graph.blocks.size());
    for (const auto& bundle : copies.bundles) {
      budget.Step();
      if (const auto* edge = std::get_if<EdgeId>(&bundle.incoming))
        copied_[edge->value] = !bundle.schedule.steps.empty();
    }
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      budget.Step();
      const auto& block = graph.blocks[b];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget.Step();
        const OpId id{block.first_op.value + i};
        if ((guards.empty() || !guards[id.value]) &&
            !Inert(graph.ops[id.value])) last_[b] = id;
      }
      inert[b] = !last_[b] && (graph.phases.empty() ||
                               graph.phases[b].phase != ExceptionPhase::kCatch);
    }
    std::size_t size = 0;
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      budget.Step();
      if (!inert[b]) continue;
      const auto& block = graph.blocks[b];
      bool normal = false;
      for (std::uint32_t i = 0; i < block.edge_count; ++i) {
        budget.Step();
        const EdgeId edge{block.first_edge.value + i};
        const auto& flow = graph.edges[edge.value];
        if (flow.kind == FlowEdgeKind::kException) continue;
        normal = true;
        if (copied_[edge.value]) {
          if (endpoints_[b] && endpoints_[b] != edge) failed[b] = 1;
          endpoints_[b] = edge;
        } else if (!inert[flow.target.value])
          failed[b] = 1;
        else {
          ++pending[b];
          next[edge.value] = heads[flow.target.value];
          heads[flow.target.value] = edge;
        }
      }
      failed[b] |= !normal;
      if (!pending[b] || failed[b]) {
        queue[size++] = WorkBlockId{static_cast<std::uint32_t>(b)};
        queued[b] = 1;
      }
    }
    // A backwards must-reach analysis: every normal successor must end at the
    // same original copy edge. Cycles/divergence cannot move an earlier getter
    // to an edge it might never reach. No opcode tail or single-entry pattern.
    for (std::size_t first = 0; first < size; ++first) {
      budget.Step();
      const auto block = queue[first];
      if (failed[block.value]) endpoints_[block.value].reset();
      for (auto edge = heads[block.value]; edge; edge = next[edge->value]) {
        budget.Step();
        const auto parent = graph.edges[edge->value].source;
        if (queued[parent.value]) continue;
        if (!endpoints_[block.value] ||
            (endpoints_[parent.value] &&
             endpoints_[parent.value] != endpoints_[block.value]))
          failed[parent.value] = 1;
        else
          endpoints_[parent.value] = endpoints_[block.value];
        --pending[parent.value];
        if (!pending[parent.value] || failed[parent.value]) {
          queue[size++] = parent;
          queued[parent.value] = 1;
        }
      }
    }
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      budget.Step();
      if (!queued[b]) endpoints_[b].reset();
    }
  }
  bool Reaches(OpId last, EdgeId target) {
    budget_.Step();
    const auto& op = graph_.ops[last.value];
    if (!graph_.phases.empty()) {
      const auto* origin =
          op.result
              ? std::get_if<OpResult>(&graph_.values[op.result->value].origin)
              : nullptr;
      // A protocol guard may have no result; its phase still names the original
      // Commit. Source evaluation retains all original pre-throw staging.
      if (origin && origin->commit)
        return endpoints_[origin->commit->value] == target;
      const auto& block = graph_.blocks[op.block.value];
      for (std::uint32_t i = 0; i < block.edge_count; ++i) {
        budget_.Step();
        const auto& edge = graph_.edges[block.first_edge.value + i];
        if (edge.kind != FlowEdgeKind::kException &&
            graph_.phases[edge.target.value].phase == ExceptionPhase::kCommit &&
            graph_.phases[edge.target.value].operation == last)
          return endpoints_[edge.target.value] == target;
      }
      return false;
    }
    // For normal blocks the producer's owned literal/guard interval may have
    // effects; everything after its end must be inert. Inter-block tails use
    // the same all-path endpoint index as the phased Commit query.
    if (last_[op.block.value] && *last_[op.block.value] > last) return false;
    const auto& block = graph_.blocks[op.block.value];
    bool found = false;
    for (std::uint32_t i = 0; i < block.edge_count; ++i) {
      budget_.Step();
      const EdgeId id{block.first_edge.value + i};
      const auto& edge = graph_.edges[id.value];
      if (edge.kind == FlowEdgeKind::kException) continue;
      found = true;
      if ((copied_[id.value] ? std::optional{id}
                             : endpoints_[edge.target.value]) != target)
        return false;
    }
    return found;
  }

 private:
  const ValueProgram& graph_;
  TreeCheckBudget& budget_;
  std::vector<std::uint8_t> copied_;
  std::vector<std::optional<OpId>> last_;
  std::vector<std::optional<EdgeId>> endpoints_;
};

std::vector<std::optional<analysis::UseId>> IndexPhiInputs(
    const analysis::ValueProgram& graph, const PhiCopies& copies,
    std::span<const std::optional<analysis::OpId>> module_guards,
    std::span<const std::optional<analysis::OpId>> literal_ends,
    TreeCheckBudget& budget, std::span<const analysis::ValueId> aliases,
    std::span<const std::uint8_t> guards = {}) {
  using namespace analysis;
  PhiTails tails(graph, copies, budget, guards);
  std::vector<std::uint8_t> identity_inputs;
  budget.Array(identity_inputs,graph.uses.size());
  for (std::size_t i=0;i<copies.inputs.size();++i) {
    budget.Step(); if(copies.projection[i] != PhiCopyUse::kRetained) identity_inputs[copies.inputs[i].value] = 1;
  }
  std::vector<PhiObservation> observations;
  budget.Array(observations, graph.ops.size());
  std::vector<std::optional<UseId>> inputs;
  budget.Array(inputs, graph.ops.size());
  const auto canonical = [&](const ValueOperand& operand) -> std::optional<ValueId> {
    const auto* value = std::get_if<ValueId>(&operand);
    return value ? std::optional{aliases.empty() ? *value : aliases[value->value]} : std::nullopt;
  };
  // One census of original observations. Checked module guards and native
  // literal definitions belong to their expression, not additional reads.
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step();
    const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || identity_inputs[i]) continue;
    if (const auto* owner = std::get_if<OpId>(&use.owner);
        owner && !guards.empty() && guards[owner->value]) continue;
    // Checked flat SSA identities are transport, not another execution or
    // observation. Keep the original phi UseId while indexing its producer.
    if (!aliases.empty()) {
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        const auto& op = graph.ops[owner->value];
        if (std::holds_alternative<ValueCopy>(op.operation) && op.result &&
            aliases[op.result->value] != *op.result) continue;
      }
    }
    const auto value = canonical(use.operand);
    const auto* producer = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!producer) continue;
    const auto id = producer->op;
    const auto guard =
        module_guards.empty() ? std::nullopt : module_guards[id.value];
    if (guard && i == graph.ops[guard->value].first_use.value &&
        std::holds_alternative<ValueModuleRead>(graph.ops[id.value].operation))
      continue;
    const auto literal =
        literal_ends.empty() ? std::nullopt : literal_ends[id.value];
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (literal && owner && id < *owner && *owner <= *literal &&
        use.role == OperandRole::kBase &&
        i == graph.ops[owner->value].first_use.value &&
        (std::holds_alternative<ValueOwnWrite>(
             graph.ops[owner->value].operation) ||
         std::holds_alternative<ValueIndexedWrite>(
             graph.ops[owner->value].operation)))
      continue;
    auto& occurrence = observations[id.value];
    if (occurrence.first)
      occurrence.multiple = true;
    else
      occurrence.first = UseId{static_cast<std::uint32_t>(i)};
  }
  for (const auto& bundle : copies.bundles) {
    budget.Step();
    if (!bundle.expression_input || bundle.schedule.steps.size() != 1 ||
        bundle.schedule.temporaries)
      continue;
    const auto* edge = std::get_if<EdgeId>(&bundle.incoming);
    if (!edge) continue;
    const auto input = *bundle.expression_input;
    const auto value = canonical(graph.uses[input.value].operand);
    const auto* producer = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!producer) continue;
    const auto id = producer->op;
    const auto& op = graph.ops[id.value];
    const auto shape = DescribeEvaluation(op.operation);
    if (op.life != ValueLife::kLive || !shape || !shape->expression ||
        observations[id.value].multiple ||
        observations[id.value].first != input)
      continue;
    auto last = id;
    if (!module_guards.empty() &&
        std::holds_alternative<ValueModuleRead>(op.operation) &&
        module_guards[id.value])
      last = *module_guards[id.value];
    if (!literal_ends.empty() && literal_ends[id.value])
      last = *literal_ends[id.value];
    if (tails.Reaches(last, *edge)) inputs[id.value] = input;
  }
  return inputs;
}
}  // namespace

std::vector<std::optional<analysis::UseId>> IndexPhiExpressionInputs(
    const analysis::ValueProgram& graph, const PhiCopies& copies,
    std::span<const std::optional<analysis::OpId>> module_guards,
    std::span<const std::optional<analysis::OpId>> literal_ends,
    TreeCheckBudget& budget, std::span<const analysis::ValueId> aliases,
    const EvaluationPlan* source) {
  auto result = IndexPhiInputs(graph, copies, module_guards, literal_ends,
                               budget, aliases);
  if (!source || !graph.phases.empty() || !source->cells) return result;
  const ReferenceArguments references(graph, *source, budget);
  budget.Step(references.calls.size());
  if (std::none_of(references.calls.begin(), references.calls.end(),
                   [](auto call) { return call != 0; })) return result;
  const auto canonical = [&](ValueOperand operand) -> std::optional<ValueId> {
    const auto* value = std::get_if<ValueId>(&operand);
    return value ? std::optional{aliases.empty() ? *value : aliases[value->value]}
                 : std::nullopt;
  };
  // Propagate the argument demand through original phi dependencies once.
  // No guessed source names, selection count or per-call graph traversal.
  std::vector<std::uint8_t> selected;
  std::vector<std::optional<UseId>> heads, next;
  std::vector<ValueId> queue;
  budget.Array(selected, graph.values.size());
  budget.Array(heads, graph.values.size());
  budget.Array(next, graph.uses.size());
  budget.Array(queue, graph.values.size());
  std::size_t first = 0, end = 0;
  const auto select = [&](ValueId value) {
    if (selected[value.value] ||
        !std::holds_alternative<PhiDefinition>(graph.values[value.value].origin)) return;
    selected[value.value] = 1; queue[end++] = value;
  };
  for (std::uint32_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step(); const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive) continue;
    if (const auto* owner = std::get_if<ValueId>(&use.owner)) {
      next[u] = heads[owner->value]; heads[owner->value] = UseId{u};
    }
    if (references.Contains(use))
      if (const auto value = canonical(use.operand)) select(*value);
  }
  while (first < end) {
    budget.Step(); const auto value = queue[first++];
    for (auto u = heads[value.value]; u; u = next[u->value]) {
      budget.Step(); if (const auto input = canonical(graph.uses[u->value].operand)) select(*input);
    }
  }
  if (!end) return result;
  std::vector<std::optional<UseId>> demands;
  std::vector<std::optional<OpId>> guards;
  std::vector<std::optional<std::size_t>> exits;
  std::vector<std::uint8_t> multiple, mask;
  budget.Array(demands, graph.ops.size()); budget.Array(guards, graph.ops.size());
  budget.Array(exits, graph.ops.size()); budget.Array(multiple, graph.ops.size());
  budget.Array(mask, graph.ops.size());
  for (const auto& bundle : copies.bundles) {
    budget.Step(); if (!bundle.expression_input) continue;
    const auto u = *bundle.expression_input; const auto& use = graph.uses[u.value];
    const auto* owner = std::get_if<ValueId>(&use.owner);
    if (!owner || !selected[owner->value]) continue;
    const auto value = canonical(use.operand);
    const auto* producer = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!producer || result[producer->op.value]) continue;
    if (demands[producer->op.value]) multiple[producer->op.value] = 1;
    demands[producer->op.value] = u;
  }
  for (std::size_t e = 0; e < graph.exceptional_exits.size(); ++e) {
    budget.Step(); const auto op = graph.exceptional_exits[e].op;
    if (exits[op.value]) throw EvaluationError::kOwnership;
    exits[op.value] = e;
  }
  bool any = false;
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const OpId guard{i};
    if (BeforeLogicalFusion(graph, *source, i).kind != EvaluationKind::kCellGuard) continue;
    const auto checked = InitializedCellGuardInput(graph, *source->cells, guard);
    const auto value = checked ? canonical(*checked) : std::nullopt;
    const auto* producer = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!producer || !demands[producer->op.value]) continue;
    if (guards[producer->op.value]) multiple[producer->op.value] = 1;
    guards[producer->op.value] = guard; any = true;
  }
  if (!any) return result;
  auto checked = VerifyValueFlow(graph, budget.Remaining());
  const auto* flow = std::get_if<ValueFlowProof>(&checked);
  if (!flow) {
    const auto error = std::get<ValueAvailabilityError>(checked);
    if (error == ValueAvailabilityError::kResourceLimit) throw EvaluationError::kResourceLimit;
    if (error == ValueAvailabilityError::kWorkLimit) throw EvaluationError::kWorkLimit;
    throw EvaluationError::kOwnership;
  }
  budget.Admit(flow->allocated_bytes, 1); budget.Step(flow->work);
  // Same finite predecessor-event lattice as analysis/guarded_reads.cc.
  // A second candidate's guard is still a foreign event for this pair: never
  // hide it merely because its own eventual phi transport might be rejected.
  enum class LastKind : std::uint8_t { kBottom, kEvent, kConflict };
  struct Last {
    LastKind kind = LastKind::kBottom;
    OpId op;
    bool operator==(const Last&) const = default;
  };
  const auto join = [](Last a, Last b) {
    if (a.kind == LastKind::kBottom) return b;
    if (b.kind == LastKind::kBottom || a == b) return a;
    return Last{LastKind::kConflict, {}};
  };
  const auto event = [](const ValueOp& op) {
    return op.life == ValueLife::kLive &&
        (op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
         op.exceptional != ExceptionalCompletion::kImpossible || IsEnvironmentState(op.operation));
  };
  std::vector<Last> preceding;
  std::vector<std::optional<OpId>> final, before;
  std::vector<WorkBlockId> pending;
  budget.Array(preceding, graph.blocks.size()); budget.Array(final, graph.blocks.size());
  budget.Array(before, graph.ops.size());
  if (graph.blocks.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2)
    throw EvaluationError::kResourceLimit;
  budget.Array(pending, 2 * graph.blocks.size() + 1);
  std::size_t pending_end = 0;
  for (std::uint32_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); if (!flow->dominators.nodes[b].begin) continue;
    const auto& block = graph.blocks[b];
    for (std::uint32_t n = 0; n < block.op_count; ++n) {
      budget.Step(); const OpId id{block.first_op.value + n};
      if (event(graph.ops[id.value])) final[b] = id;
    }
    if (b == 0) preceding[b] = {LastKind::kConflict, {}};
    if (b == 0 || final[b]) pending[pending_end++] = {b};
  }
  for (std::size_t head = 0; head < pending_end; ++head) {
    budget.Step(); const auto b = pending[head].value;
    const auto output = final[b] ? Last{LastKind::kEvent, *final[b]} : preceding[b];
    const auto& block = graph.blocks[b];
    for (std::uint32_t e = 0; e < block.edge_count; ++e) {
      budget.Step(); const auto& edge = graph.edges[block.first_edge.value + e];
      if (edge.kind == FlowEdgeKind::kException) continue;
      auto& input = preceding[edge.target.value]; const auto next = join(input, output);
      if (next == input) continue;
      input = next;
      if (!final[edge.target.value]) {
        if (pending_end == pending.size()) throw EvaluationError::kWorkLimit;
        pending[pending_end++] = edge.target;
      }
    }
  }
  for (std::uint32_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); auto prior = preceding[b]; const auto& block = graph.blocks[b];
    for (std::uint32_t n = 0; n < block.op_count; ++n) {
      budget.Step(); const OpId id{block.first_op.value + n};
      if (prior.kind == LastKind::kEvent) before[id.value] = prior.op;
      if (event(graph.ops[id.value])) prior = {LastKind::kEvent, id};
    }
  }
  // This is a native binding-read obligation, not an initialized-cell premise.
  // CellEvaluation/its real enclosure still own TDZ and storage identity. The
  // guard must execute before this exact phi edge; the projected all-path tail
  // below additionally forbids every intervening read/write/call/environment
  // effect, copy schedule or divergence. Its exception must still go to caller.
  for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!guards[i] || multiple[i]) continue;
    const auto guard = *guards[i]; const auto& op = graph.ops[guard.value];
    const auto& use = graph.uses[demands[i]->value];
    const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
    if (!edge || !exits[guard.value]) continue;
    const auto& exit = graph.exceptional_exits[*exits[guard.value]];
    if (before[guard.value] != OpId{i} || exit.handler || exit.origin != op.origin || exit.before_throw != op.before_throw ||
        op.exceptional != ExceptionalCompletion::kMayThrow ||
        op.normal != LanguageEffect::kGuard || op.before_throw != LanguageEffect::kGuard ||
        !flow->dominators.Dominates({op.block.value}, {graph.edges[edge->value].source.value}) ||
        (op.block == graph.ops[i].block && op.order <= graph.ops[i].order)) continue;
    mask[guard.value] = 1;
  }
  auto projected = IndexPhiInputs(graph, copies, module_guards, literal_ends,
                                  budget, aliases, mask);
  for (std::uint32_t i = 0; i < result.size(); ++i) {
    budget.Step();
    if (!result[i] && guards[i] && mask[guards[i]->value]) result[i] = projected[i];
  }
  return result;
}
}  // namespace abcd::source
