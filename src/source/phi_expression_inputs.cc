#include "evaluation_plan.h"
#include "phi_copies.h"
#include "tree_checks.h"

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
           TreeCheckBudget& budget)
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
        if (!Inert(graph.ops[id.value])) last_[b] = id;
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
}  // namespace

std::vector<std::optional<analysis::UseId>> IndexPhiExpressionInputs(
    const analysis::ValueProgram& graph, const PhiCopies& copies,
    std::span<const std::optional<analysis::OpId>> module_guards,
    std::span<const std::optional<analysis::OpId>> literal_ends,
    TreeCheckBudget& budget) {
  using namespace analysis;
  PhiTails tails(graph, copies, budget);
  std::vector<std::uint8_t> identity_inputs;
  budget.Array(identity_inputs,graph.uses.size());
  for (std::size_t i=0;i<copies.inputs.size();++i) {
    budget.Step(); if(copies.projection[i] == PhiCopyUse::kIdentity) identity_inputs[copies.inputs[i].value] = 1;
  }
  std::vector<PhiObservation> observations;
  budget.Array(observations, graph.ops.size());
  std::vector<std::optional<UseId>> inputs;
  budget.Array(inputs, graph.ops.size());
  // One census of original observations. Checked module guards and native
  // literal definitions belong to their expression, not additional reads.
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step();
    const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || identity_inputs[i]) continue;
    const auto* value = std::get_if<ValueId>(&use.operand);
    const auto* producer =
        value ? std::get_if<OpResult>(&graph.values[value->value].origin)
              : nullptr;
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
    const auto* value = std::get_if<ValueId>(&graph.uses[input.value].operand);
    const auto* producer =
        value ? std::get_if<OpResult>(&graph.values[value->value].origin)
              : nullptr;
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
}  // namespace abcd::source
