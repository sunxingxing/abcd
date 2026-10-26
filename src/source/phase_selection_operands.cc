#include <algorithm>

#include "../analysis/value_flow.h"
#include "evaluation_aliases.h"
#include "evaluation_liveness.h"
#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_corridor.h"
#include "phase_predicate_prefix.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value) { if (!value) throw EvaluationError::kOwnership; }
bool Pure(const ValueOp& op) {
  return op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible;
}
std::span<const SelectionRegion> PrefixRegions(PhasePrefixDomain domain,
    std::span<const SelectionRegion> regions) {
  switch (domain) {
    case PhasePrefixDomain::kPredicate: return {};
    case PhasePrefixDomain::kSelection: return regions;
  }
  throw EvaluationError::kOwnership;
}
class OperandSelections {
 public:
  OperandSelections(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget,
      std::span<const SelectionRegion> regions)
      : graph_(graph), plan_(plan), budget_(budget), phase_budget_(budget.Remaining()),
        corridors_(graph, plan, phase_budget_) {
    budget.Admit(phase_budget_.Cost().bytes, 1);
    accounted_work_ = phase_budget_.Cost().work; budget.Step(accounted_work_);
    budget.Array(uses_, graph.values.size()); budget.Array(incoming_, graph.blocks.size());
    budget.Array(copied_, graph.edges.size());
    budget.Array(callees_, graph.values.size()); budget.Array(prefix_, graph.ops.size());
    budget.Array(heads_, graph.values.size()); budget.Array(next_, graph.uses.size());
    budget.Array(owners_, graph.ops.size());
    budget.Array(transport_owners_, graph.ops.size());
    budget.Array(ancestor_checked_, graph.ops.size());
    roots_ = IndexPhaseInlineRoots(graph, plan, budget);
    for (const auto& edge : graph.edges) {
      budget.Step(); if (edge.kind != FlowEdgeKind::kException) ++incoming_[edge.target.value];
    }
    if (plan.phis) for (const auto& bundle : plan.phis->bundles) {
      budget.Step(); if (const auto* edge = std::get_if<EdgeId>(&bundle.incoming)) copied_[edge->value] = !bundle.schedule.steps.empty();
    }
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step(); const auto& use = graph.uses[i];
      if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
      if (const auto* op = std::get_if<OpId>(&use.owner)) {
        const auto kind = BeforeLogicalFusion(graph, plan, op->value).kind;
        if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure) continue;
      }
      const auto operand = ResolveEvaluationOperand(plan, use.operand);
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        if (use.role == OperandRole::kCallee) {
          if (const auto* owner = std::get_if<OpId>(&use.owner)) {
            const auto* invoke = std::get_if<ValueInvoke>(&graph.ops[owner->value].operation);
            if (invoke && invoke->receiver == InvokeReceiver::kExplicit) callees_[value->value] = 1;
          }
        }
        next_[i] = heads_[value->value]; heads_[value->value] = UseId{static_cast<std::uint32_t>(i)};
        auto& uses = uses_[value->value];
        if (uses.first) uses.multiple = true;
        else uses.first = UseId{static_cast<std::uint32_t>(i)};
      }
    }
    IndexReferences();
    predicates_ = IndexPhasePredicatePrefixes(graph, plan, roots_, budget,
        PrefixRegions(plan.phase_prefix_domain, regions));
  }

  struct OperandSelection {
    std::size_t region;
    OpId consumer;
    UseId input;
    std::optional<OpId> transport;
  };
  std::optional<OperandSelection> Operand(const SelectionRegion& region, std::size_t index) {
    if (region.parent || !region.complete_phi) return {};
    const auto& users = uses_[region.merged.value];
    if (!users.first || users.multiple) return {};
    auto input = *users.first;
    std::optional<OpId> transport;
    if (const auto* owner = std::get_if<OpId>(&graph_.uses[input.value].owner)) {
      const auto& op = graph_.ops[owner->value];
      if (std::holds_alternative<ValueCopy>(op.operation)) {
        // A transparent SSA copy transports the selected value even when the
        // common evaluator already owns it inside its consumer. Root/inline
        // is representation, not identity or an execution-count obligation.
        // Canonical uses and the shared prefix/scope proof remain authoritative.
        if (!Pure(op) || !op.result || op.use_count != 1 || op.first_use != input) return {};
        const auto& next = uses_[op.result->value];
        if (!next.first || next.multiple) return {};
        transport = *owner; input = *next.first;
      }
    }
    const auto& use = graph_.uses[input.value];
    const auto* consumer = std::get_if<OpId>(&use.owner);
    if (!consumer) return {};
    const auto& operation = graph_.ops[consumer->value];
    const auto shape = DescribeEvaluation(operation.operation);
    if (!shape || shape->inputs != InputEvaluation::kEager || shape->NeedsSnapshot(use.role) ||
        use.role == OperandRole::kReceiver || PredicateSnapshot(graph_, operation, use.operand)) return {};
    const bool member_argument = use.role == OperandRole::kArgument &&
        std::holds_alternative<ValueInvoke>(operation.operation);
    if (!member_argument && !PhaseCorridors::ConsumesOperands(operation.operation)) return {};
    return OperandSelection{index, *consumer, input, transport};
  }
  std::vector<PhaseSelectionOperand> CheckAll(std::span<const SelectionRegion> regions) {
    std::vector<OperandSelection> operands;
    budget_.Admit(regions.size(), sizeof(OperandSelection)); operands.reserve(regions.size());
    budget_.Admit(operands.capacity() - regions.size(), sizeof(OperandSelection));
    for (std::size_t i = 0; i < regions.size(); ++i) {
      budget_.Step(); if (auto input = Operand(regions[i], i)) {
        operands.push_back(*input);
        if (input->transport) transport_owners_[input->transport->value] = input->consumer;
      }
    }
    std::sort(operands.begin(), operands.end(), [&](const auto& a, const auto& b) {
      budget_.Step(); return a.consumer != b.consumer ? a.consumer < b.consumer : a.input < b.input;
    });
    std::vector<PhaseSelectionOperand> selected;
    budget_.Admit(regions.size(), sizeof(PhaseSelectionOperand)); selected.reserve(regions.size());
    budget_.Admit(selected.capacity() - regions.size(), sizeof(PhaseSelectionOperand));
    struct Group {
      std::size_t first, end;
      std::optional<std::size_t> parent, child, sibling;
      std::optional<OpId> execution;
    };
    std::vector<Group> groups;
    std::vector<std::optional<std::size_t>> branches;
    std::vector<std::size_t> queue;
    budget_.Array(branches, graph_.ops.size()); budget_.Array(queue, operands.size());
    budget_.Admit(operands.size(), sizeof(Group)); groups.reserve(operands.size());
    budget_.Admit(groups.capacity() - operands.size(), sizeof(Group));
    for (std::size_t first = 0; first < operands.size();) {
      auto end = first + 1;
      while (end < operands.size() && operands[end].consumer == operands[first].consumer) { budget_.Step(); ++end; }
      for (auto i = first; i < end; ++i) {
        budget_.Step(); const auto branch = regions[operands[i].region].branch;
        Require(!branches[branch.value]); branches[branch.value] = groups.size();
      }
      groups.push_back({first, end}); first = end;
    }
    std::size_t begin = 0, finish = 0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
      budget_.Step(); auto& group = groups[i];
      group.parent = branches[roots_[operands[group.first].consumer.value].value];
      if (group.parent) {
        group.sibling = groups[*group.parent].child; groups[*group.parent].child = i;
      } else queue[finish++] = i;
    }
    std::vector<std::optional<std::size_t>> region_indices;
    std::vector<std::size_t> nested;
    budget_.Array(region_indices, graph_.ops.size()); budget_.Array(nested, regions.size());
    for (std::size_t i = 0; i < regions.size(); ++i) { budget_.Step(); region_indices[regions[i].branch.value] = i; }
    // Resolve actual source dependencies from the outside inward. A child's
    // pure syntax consumer cannot supply a throw dispatch: only a successfully
    // checked enclosing selection or the original inline root supplies its
    // execution scope. Cycles and failed parents remain unowned. Group siblings
    // retain operand order and the existing joint interval obligations.
    while (begin < finish) {
      budget_.Step(); auto& group = groups[queue[begin++]];
      const auto first = group.first, end = group.end;
      const auto execution = group.parent ? groups[*group.parent].execution :
          std::optional{roots_[operands[first].consumer.value]};
      const auto base = selected.size();
      std::optional<WorkBlockId> previous;
      bool complete = execution.has_value();
      for (auto i = first; i < end && complete; ++i) {
        budget_.Step(); const auto& region = regions[operands[i].region];
        if (i != first && operands[i - 1].input == operands[i].input) { complete = false; break; }
        auto entry = Check(region, operands[i], previous, *execution);
        if (!entry) { complete = false; break; }
        previous = region.join; selected.push_back(std::move(*entry));
        std::size_t child_begin = 0, child_end = 1; nested[0] = operands[i].region;
        while (child_begin < child_end && complete) {
          budget_.Step(); const auto& parent = regions[nested[child_begin++]];
          for (const auto* arm : {&parent.right, &*parent.consequent}) {
            const auto* child = std::get_if<SelectionChild>(arm); if (!child) continue;
            Require(region_indices[child->branch.value].has_value());
            const auto index = *region_indices[child->branch.value];
            Require(regions[index].parent == parent.branch);
            auto entry = Child(regions[index], *execution);
            if (!entry) { complete = false; break; }
            nested[child_end++] = index; selected.push_back(std::move(*entry));
          }
        }
      }
      if (complete) complete = Inert(*previous, graph_.ops[operands[first].consumer.value].block, true);
      if (!complete) selected.resize(base);
      else {
        group.execution = execution;
        if (std::holds_alternative<PhaseCallReference>(selected[base].owner)) Ancestors(selected[base]);
      }
      for (auto child = group.child; child; child = groups[*child].sibling) {
        budget_.Step(); queue[finish++] = *child;
      }
    }
    std::sort(selected.begin(), selected.end(), [&](const auto& a, const auto& b) {
      budget_.Step(); return a.branch < b.branch;
    });
    return selected;
  }
  std::vector<PhaseSelectionOperand> Bindings(std::span<const SelectionRegion> regions) {
    std::vector<PhaseSelectionOperand> selected;
    budget_.Admit(regions.size(), sizeof(PhaseSelectionOperand)); selected.reserve(regions.size());
    budget_.Admit(selected.capacity() - regions.size(), sizeof(PhaseSelectionOperand));
    std::vector<std::optional<std::size_t>> region_indices;
    std::vector<std::size_t> nested;
    budget_.Array(region_indices, graph_.ops.size()); budget_.Array(nested, regions.size());
    for (std::size_t i = 0; i < regions.size(); ++i) { budget_.Step(); region_indices[regions[i].branch.value] = i; }
    // Unfused families retain their original control position and phi slot.
    // A later observation need not be eager or unique: it reads that slot.
    // Root predicate snapshots remain before the original branch; only child
    // prefixes are carried inside their original selected arms. Actual source
    // control binding and ExpressionScopes independently prove the same catch.
    std::vector<std::uint8_t> owned; budget_.Array(owned, graph_.ops.size());
    for (const auto& entry : plan_.phase_selections) { budget_.Step(); owned[entry.branch.value] = 1; }
    for (std::size_t i = 0; i < regions.size(); ++i) {
      budget_.Step(); const auto& root = regions[i];
      if (root.parent || !root.complete_phi || owned[root.branch.value]) continue;
      const auto base = selected.size();
      if (!Scope(root, root.branch, 0, Placement::kOriginal)) continue;
      selected.push_back({PhaseSelectionBinding{}, root.branch, root.merged, {}, {}, root.branch});
      std::size_t child_begin = 0, child_end = 1; nested[0] = i; bool complete = true;
      while (child_begin < child_end && complete) {
        budget_.Step(); const auto& parent = regions[nested[child_begin++]];
        for (const auto* arm : {&parent.right, &*parent.consequent}) {
          const auto* child = std::get_if<SelectionChild>(arm); if (!child) continue;
          Require(region_indices[child->branch.value].has_value());
          const auto index = *region_indices[child->branch.value];
          Require(regions[index].parent == parent.branch);
          auto entry = Child(regions[index], root.branch, Placement::kOriginal);
          if (!entry) { complete = false; break; }
          nested[child_end++] = index; selected.push_back(std::move(*entry));
        }
      }
      if (!complete) selected.resize(base);
      else for (auto at = base; at < selected.size(); ++at) {
        budget_.Step(); Require(!owned[selected[at].branch.value]); owned[selected[at].branch.value] = 1;
      }
    }
    return selected;
  }
  std::optional<PhaseSelectionOperand> Check(const SelectionRegion& region, const OperandSelection& selected_operand,
      std::optional<WorkBlockId> previous, OpId execution) {
    const auto consumer = selected_operand.consumer;
    const auto input = selected_operand.input;
    const auto transport = selected_operand.transport;
    const auto& invoke = graph_.ops[consumer.value];
    const auto* operation = std::get_if<ValueInvoke>(&invoke.operation);
    const auto consumer_kind = BeforeLogicalFusion(graph_, plan_, consumer.value).kind;
    // A phi-input consumer is its own execution root: it owns the sole
    // executable incoming-copy RHS, whose original availability, effects and
    // exceptional staging were proved at its original edge.
    if (consumer_kind != EvaluationKind::kRoot && consumer_kind != EvaluationKind::kInline &&
        consumer_kind != EvaluationKind::kPhiInput) return {};
    std::variant<OpId, PhaseCallReference, PhaseSelectionArm, PhaseSelectionBinding> owner = consumer;
    std::size_t prefix_count = 0;
    if (operation) {
      if (operation->receiver != InvokeReceiver::kExplicit) return {};
      const auto callee_value = ResolveEvaluationOperand(plan_, graph_.uses[invoke.first_use.value].operand);
      const auto* value = std::get_if<ValueId>(&callee_value);
      const auto* result = value ? std::get_if<OpResult>(&graph_.values[value->value].origin) : nullptr;
      if (!result || !result->commit || uses_[value->value].multiple || uses_[value->value].first != invoke.first_use) return {};
      const auto callee = result->op; const auto& property = graph_.ops[callee.value];
      if ((!std::holds_alternative<ValueNamedRead>(property.operation) &&
           !std::holds_alternative<ValuePropertyRead>(property.operation)) ||
          BeforeLogicalFusion(graph_, plan_, callee.value).kind != EvaluationKind::kRoot ||
          corridors_.HasCatchCopies(callee) || corridors_.HasCatchCopies(consumer) ||
          !corridors_.SameCatch(callee, consumer)) return {};
      // Reference evaluation precedes all operands. Existing prefix ownership
      // keeps those snapshots in their original order and catch dispatch.
      if (!Prefix(previous.value_or(*result->commit), region.header, consumer, region.branch,
          prefix_count, previous.has_value())) return {};
      owner = PhaseCallReference{callee, consumer};
    } else {
      // Eager consumers need no synthetic callee. Canonical single use and
      // inert intervals keep evaluation count and order; observable operands
      // outside the selection retain their original snapshots. Reference/key
      // and repeated-input obligations were checked by the shared shape.
      if (corridors_.HasCatchCopies(consumer) ||
          (previous && !Inert(*previous, region.header, true))) return {};
      const auto count = PredicatePrefix(region, execution);
      if (!count) return {};
      prefix_count = *count;
    }
    if (!Scope(region, execution, prefix_count)) return {};
    PhaseSelectionOperand selected{owner, region.branch, region.merged, input, transport, execution};
    budget_.Array(selected.prefix, prefix_count);
    std::copy_n(prefix_.begin(), prefix_count, selected.prefix.begin());
    return selected;
  }
  enum class Placement : std::uint8_t { kConsumer, kOriginal };
  bool Scope(const SelectionRegion& region, OpId execution, std::size_t prefix_count,
      Placement placement = Placement::kConsumer) {
    for (std::size_t i = 0; i < prefix_count; ++i) owners_[prefix_[i].value] = region.branch;
    owners_[region.branch.value] = region.branch;
    for (const auto& condition : region.conditions) owners_[condition.branch.value] = region.branch;
    for (const auto* arm : {&region.right, &*region.consequent}) {
      // Snapshot inputs have no producer owned by the selected arm. Discovery
      // preserves their inert path, original edge use and available binding.
      if (std::holds_alternative<SelectionSnapshot>(*arm) || std::holds_alternative<SelectionChild>(*arm)) continue;
      const auto* input = std::get_if<ValueId>(arm);
      const auto* producer = input ? std::get_if<OpResult>(&graph_.values[input->value].origin) : nullptr;
      if (!producer) return false;
      owners_[producer->op.value] = region.branch;
      const auto& op = graph_.ops[producer->op.value];
      if ((placement == Placement::kOriginal && corridors_.HasCatchCopies(producer->op)) ||
          (placement == Placement::kConsumer && op.exceptional == ExceptionalCompletion::kMayThrow &&
           !corridors_.SameCatch(producer->op, execution))) return false;
    }
    // A moved prefix snapshot may be consumed only by this prefix, scalar
    // decision or selected arms. In particular an earlier input of an
    // enclosing consumer must not read it before the inner consumer evaluates it.
    for (std::size_t i = 0; i < prefix_count; ++i) {
      budget_.Step(); const auto value = *graph_.ops[prefix_[i].value].result;
      for (auto at = heads_[value.value]; at; at = next_[at->value]) {
        budget_.Step(); const auto& use = graph_.uses[at->value];
        if (const auto* owner = std::get_if<OpId>(&use.owner);
            owner && owners_[roots_[owner->value].value] == region.branch) continue;
        // A retained selected-arm input is an actual observation on this
        // region's phi edge. It belongs to the same input expression, not
        // to an external phi or an enclosing consumer's earlier input.
        const auto* merged = std::get_if<ValueId>(&use.owner);
        const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
        if (!merged || *merged != region.merged || use.role != OperandRole::kPhiInput ||
            !edge || (*edge != region.keep_input && *edge != region.evaluate_input)) return false;
      }
    }
    return true;
  }
  bool CertifiedLiteralCondition(const SelectionRegion& region) const {
    if (plan_.literal_arm_inputs.empty() || !region.consequent) return false;
    for (const auto* arm : {&region.right, &*region.consequent}) {
      const auto* value = std::get_if<ValueId>(arm);
      const auto* origin =
          value ? std::get_if<OpResult>(&graph_.values[value->value].origin)
                : nullptr;
      if (!origin || origin->op.value >= plan_.literal_arm_inputs.size())
        continue;
      const auto input = plan_.literal_arm_inputs[origin->op.value];
      if (!input) continue;
      const auto& use = graph_.uses[input->value];
      const auto* merged = std::get_if<ValueId>(&use.owner);
      if (merged && *merged == region.merged) return true;
    }
    return false;
  }
  std::optional<std::size_t> PredicatePrefix(const SelectionRegion& region, OpId execution, Placement placement = Placement::kConsumer) {
    const auto range = predicates_.ranges[region.branch.value];
    std::size_t count = 0, retained = 0;
    const bool can_retain =
        placement == Placement::kConsumer && CertifiedLiteralCondition(region);
    for (std::size_t p = 0; p < range.count; ++p) {
      budget_.Step(); const auto id = predicates_.operations[range.first + p];
      const auto next = p + 1 == range.count ? region.branch : predicates_.operations[range.first + p + 1];
      const auto& op = graph_.ops[id.value];
      const bool adjacent =
          predicates_.order.after[id.value] == next &&
          predicates_.order.before[next.value] == EffectPredecessor{id};
      if (!adjacent && can_retain) {
        // Exclusive use is not permission to replay a retained call/getter
        // across a saved Reference or private allocation. Keep the original
        // materialized prefix and compose only its contiguous movable suffix.
        // Each omitted root is checked once; SSA Commit availability proves
        // the condition reads the already initialized language snapshot.
        for (; retained <= p; ++retained) {
          budget_.Step();
          const auto saved = predicates_.operations[range.first + retained];
          const auto& root = graph_.ops[saved.value];
          const auto& branch = graph_.ops[region.branch.value];
          if (!root.result ||
              !HasRetainedEvaluationValue(graph_, plan_, *root.result) ||
              !ValueAvailableAt(graph_, predicates_.order.dominators,
                                *root.result, branch.block, branch.order))
            return {};
        }
        count = 0;
        continue;
      }
      if (!adjacent ||
          !predicates_.order.Before(graph_, id,
                                    graph_.ops[region.branch.value].block,
                                    graph_.ops[region.branch.value].order) ||
          (placement == Placement::kOriginal &&
           corridors_.HasCatchCopies(id)) ||
          (placement == Placement::kConsumer &&
           op.exceptional == ExceptionalCompletion::kMayThrow &&
           !corridors_.SameCatch(id, execution)))
        return {};
      Require(count < prefix_.size()); prefix_[count++] = id;
    }
    return count;
  }
  std::optional<PhaseSelectionOperand> Child(const SelectionRegion& region, OpId execution, Placement placement = Placement::kConsumer) {
    Require(region.parent.has_value());
    const auto count = PredicatePrefix(region, execution, placement);
    if (!count || !Scope(region, execution, *count, placement)) return {};
    PhaseSelectionOperand result{PhaseSelectionArm{*region.parent}, region.branch, region.merged, {}, {}, execution};
    budget_.Array(result.prefix, *count); std::copy_n(prefix_.begin(), *count, result.prefix.begin());
    return result;
  }
  void Account() {
    budget_.Step(phase_budget_.Cost().work - accounted_work_);
  }
 private:
  struct ReferencePosition {
    std::optional<WorkBlockId> head;
    std::size_t order = 0, effects = 0, run_start = 0;
    std::optional<OpId> root;
  };
  struct ReferenceExit {
    std::optional<WorkBlockId> target;
    ReferencePosition position;
  };
  void IndexReferences() {
    std::vector<std::optional<EdgeId>> next, exits;
    std::vector<std::uint8_t> previous;
    budget_.Array(next, graph_.blocks.size()); budget_.Array(exits, graph_.blocks.size());
    budget_.Array(previous, graph_.blocks.size());
    budget_.Array(reference_positions_, graph_.blocks.size());
    budget_.Array(reference_exits_, graph_.blocks.size());
    budget_.Array(reference_bad_catches_, graph_.ops.size());
    for (std::size_t b = 0; b < graph_.blocks.size(); ++b) {
      budget_.Step(); const auto& block = graph_.blocks[b];
      if (graph_.phases[b].phase == ExceptionPhase::kCatch) continue;
      std::optional<EdgeId> successor; bool multiple = false;
      for (std::uint32_t i = 0; i < block.edge_count; ++i) {
        budget_.Step(); const EdgeId edge{block.first_edge.value + i};
        if (graph_.edges[edge.value].kind == FlowEdgeKind::kException) continue;
        if (successor) multiple = true;
        successor = edge;
      }
      if (multiple || !successor || copied_[successor->value]) continue;
      exits[b] = successor;
      const auto target = graph_.edges[successor->value].target;
      if (incoming_[target.value] != 1 || graph_.phases[target.value].phase == ExceptionPhase::kCatch) continue;
      next[b] = successor; previous[target.value] = 1;
    }
    // Disjoint normal corridors carry the last homogeneous effect-owner run.
    // O(B+E+I) indexing makes foreign-effect/copy/order refusals O(1), including
    // many sibling arguments with a shared long inert prefix. Pure closed
    // cycles remain unindexed, as in the common phase corridor index.
    for (std::size_t head = 0; head < graph_.blocks.size(); ++head) {
      budget_.Step(); if (previous[head]) continue;
      ReferencePosition position{WorkBlockId{static_cast<std::uint32_t>(head)}};
      auto at = head;
      for (;;) {
        budget_.Step(); Require(!reference_positions_[at].head);
        reference_positions_[at] = position;
        const auto& block = graph_.blocks[at];
        for (std::uint32_t i = 0; i < block.op_count; ++i) {
          budget_.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph_.ops[id.value];
          if (op.life != ValueLife::kLive) continue;
          const bool copies = corridors_.HasCatchCopies(id);
          if (Pure(op) && !copies) continue;
          const auto root = copies ? std::optional<OpId>{} : std::optional{roots_[id.value]};
          if (root != position.root) { position.root = root; position.run_start = position.effects; }
          ++position.effects;
          if (op.exceptional == ExceptionalCompletion::kMayThrow &&
              !corridors_.SameCatch(id, roots_[id.value])) reference_bad_catches_[roots_[id.value].value] = 1;
        }
        if (!next[at]) {
          if (exits[at]) {
            auto& exit = reference_exits_[head];
            exit.target = graph_.edges[exits[at]->value].target;
            exit.position = position; ++exit.position.order;
          }
          break;
        }
        ++position.order; at = graph_.edges[next[at]->value].target.value;
      }
    }
  }
  bool ReferencePrefix(WorkBlockId at, WorkBlockId end, OpId callee, OpId call) {
    budget_.Step(); if (at == end) return true;
    const auto& begin = reference_positions_[at.value];
    if (incoming_[at.value] != 1 || !begin.head) return false;
    const auto& exit = reference_exits_[begin.head->value];
    // The destination is observed before entering it. Its other predecessors
    // do not invalidate the original unique path, and its effects stay outside
    // this interval; never introduce a single-entry requirement at the endpoint.
    const auto& finish = exit.target == end ? exit.position : reference_positions_[end.value];
    if (begin.head != finish.head || begin.order >= finish.order) return false;
    if (begin.effects == finish.effects) return true;
    return finish.root == callee && finish.run_start <= begin.effects &&
        !reference_bad_catches_[callee.value] && corridors_.SameCatch(callee, call);
  }
  void Ancestors(PhaseSelectionOperand& selected) {
    const auto reference = std::get<PhaseCallReference>(selected.owner);
    auto child_call = reference.call, child_callee = reference.callee;
    // The actual consumer chain fixes each ancestor relation. Memoize that
    // relation once; selected records only partition storage of its union,
    // never infer ordering from selection counts or a greedy shared suffix.
    for (;;) {
      budget_.Step(); if (ancestor_checked_[child_call.value]) break;
      ancestor_checked_[child_call.value] = 1;
      budget_.Step(); const auto placement = BeforeLogicalFusion(graph_, plan_, child_call.value);
      if (placement.kind != EvaluationKind::kInline || !placement.consumer) break;
      const auto& input = graph_.uses[placement.consumer->value];
      const auto* parent = std::get_if<OpId>(&input.owner);
      if (!parent || input.role != OperandRole::kArgument) break;
      const auto& call = graph_.ops[parent->value];
      const auto* invoke = std::get_if<ValueInvoke>(&call.operation);
      if (!invoke || invoke->receiver != InvokeReceiver::kExplicit) break;
      const auto operand = ResolveEvaluationOperand(plan_, graph_.uses[call.first_use.value].operand);
      const auto* value = std::get_if<ValueId>(&operand);
      const auto* source = value ? std::get_if<OpResult>(&graph_.values[value->value].origin) : nullptr;
      if (!source || !source->commit ||
          uses_[value->value].multiple || uses_[value->value].first != call.first_use) break;
      const auto& property = graph_.ops[source->op.value];
      if ((!std::holds_alternative<ValueNamedRead>(property.operation) &&
           !std::holds_alternative<ValuePropertyRead>(property.operation)) ||
          BeforeLogicalFusion(graph_, plan_, source->op.value).kind != EvaluationKind::kRoot ||
          corridors_.HasCatchCopies(source->op) || corridors_.HasCatchCopies(*parent) ||
          !corridors_.SameCatch(source->op, *parent) || !corridors_.SameCatch(child_call, *parent) ||
          !ReferencePrefix(*source->commit, graph_.ops[child_callee.value].block, child_callee, *parent)) break;
      if (selected.ancestors.size() == selected.ancestors.capacity()) {
        Require(selected.ancestors.size() < graph_.ops.size());
        const auto capacity = selected.ancestors.empty() ? 1 : std::min(graph_.ops.size(), selected.ancestors.size() * 2);
        budget_.Admit(capacity, sizeof(PhaseCallReference)); selected.ancestors.reserve(capacity);
        budget_.Admit(selected.ancestors.capacity() - capacity, sizeof(PhaseCallReference));
      }
      selected.ancestors.push_back({source->op, *parent});
      child_call = *parent; child_callee = source->op;
    }
  }
  bool Prefix(WorkBlockId at, WorkBlockId end, OpId call, OpId branch, std::size_t& count, bool join_entry = false) {
    for (std::size_t visited = 0; visited < graph_.blocks.size(); ++visited) {
      budget_.Step(); if (at == end) return true;
      if ((incoming_[at.value] != 1 && !(join_entry && visited == 0)) || graph_.phases[at.value].phase == ExceptionPhase::kCatch) return false;
      const auto& block = graph_.blocks[at.value];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget_.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        if (corridors_.HasCatchCopies(id)) return false;
        const auto kind = BeforeLogicalFusion(graph_, plan_, id.value).kind;
        if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure ||
            id == branch || transport_owners_[id.value] == call || std::holds_alternative<ValueJump>(op.operation) || std::holds_alternative<ValueNop>(op.operation)) {
          if (!Pure(op)) return false;
          continue;
        }
        if (op.exceptional == ExceptionalCompletion::kMayThrow && !corridors_.SameCatch(id, call)) return false;
        if (kind == EvaluationKind::kInline || kind == EvaluationKind::kModuleGuard) continue;
        if (kind != EvaluationKind::kRoot || !op.result || !PhaseCorridors::Expression(op.operation) ||
            callees_[op.result->value] || count >= prefix_.size()) return false;
        prefix_[count++] = id;
      }
      std::optional<EdgeId> normal;
      for (std::uint32_t i = 0; i < block.edge_count; ++i) {
        budget_.Step(); const EdgeId edge{block.first_edge.value + i};
        if (graph_.edges[edge.value].kind == FlowEdgeKind::kException) continue;
        if (normal || copied_[edge.value]) return false;
        normal = edge;
      }
      if (!normal) return false;
      at = graph_.edges[normal->value].target;
    }
    return false;
  }
  bool Inert(WorkBlockId at, WorkBlockId end, bool join_entry = false) {
    budget_.Step(); if (at == end) return true;
    if ((incoming_[at.value] != 1 && !join_entry) || graph_.phases[at.value].phase == ExceptionPhase::kCatch) return false;
    const auto& begin = reference_positions_[at.value];
    if (!begin.head) return false;
    const auto& exit = reference_exits_[begin.head->value];
    const auto& finish = exit.target == end ? exit.position : reference_positions_[end.value];
    // The shared normal-interval index excludes edge/catch copies and cycles.
    // Comparing effect positions proves an inert suffix in O(1), even when
    // many different consumers share it; no per-selection graph walk.
    return begin.head == finish.head && begin.order < finish.order && begin.effects == finish.effects;
  }

  const ValueProgram& graph_;
  const EvaluationPlan& plan_;
  TreeCheckBudget& budget_;
  PhaseBudget phase_budget_;
  PhaseCorridors corridors_;
  std::uint64_t accounted_work_ = 0;
  std::vector<PhaseUse> uses_;
  std::vector<std::uint32_t> incoming_;
  std::vector<std::uint8_t> copied_, callees_, ancestor_checked_, reference_bad_catches_;
  std::vector<OpId> prefix_, roots_;
  std::vector<ReferencePosition> reference_positions_;
  std::vector<ReferenceExit> reference_exits_;
  std::vector<std::optional<UseId>> heads_, next_;
  std::vector<std::optional<OpId>> owners_, transport_owners_;
  PhasePredicatePrefixes predicates_;
};
}
std::vector<PhaseSelectionOperand> FindPhaseSelectionBindings(const ValueProgram& graph,
    const EvaluationPlan& plan, std::span<const SelectionRegion> regions, TreeCheckBudget& budget) {
  bool needed = false;
  for (const auto& region : regions) {
    budget.Step(); needed |= !region.parent && region.complete_phi &&
        plan.operations[region.branch.value].kind != EvaluationKind::kLogicalValue;
  }
  if (!needed) return {};
  OperandSelections proof(graph, plan, budget, regions);
  auto bindings = proof.Bindings(regions); proof.Account(); return bindings;
}
void BuildPhaseSelectionOperands(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  if (regions.empty()) return;
  OperandSelections proof(graph, plan, budget, regions);
  auto operands = proof.CheckAll(regions);
  proof.Account();
  if (operands.empty()) return;
  budget.Array(plan.logical_values, graph.values.size());
  budget.Array(plan.phase_transports, graph.ops.size());
  budget.Array(plan.phase_prefixes, graph.ops.size());
  budget.Array(plan.phase_references, graph.ops.size());
  budget.Array(plan.phase_children, graph.ops.size());
  for (std::size_t i = 0; i < operands.size(); ++i) {
    budget.Step(); const auto& selected = operands[i];
    Require(!i || operands[i - 1].branch < selected.branch);
    if (const auto* reference = std::get_if<PhaseCallReference>(&selected.owner)) {
      const auto callee = reference->callee.value;
      const auto input = graph.ops[reference->call.value].first_use;
      Require(!plan.phase_prefixes[callee]);
      Require(!plan.phase_references[callee] || plan.phase_references[callee] == input);
      plan.phase_references[callee] = input;
      plan.operations[callee] = {EvaluationKind::kInline, input, {}};
    }
    for (const auto& ancestor : selected.ancestors) {
      budget.Step(); Require(!plan.phase_references[ancestor.callee.value] ||
          plan.phase_references[ancestor.callee.value] == graph.ops[ancestor.call.value].first_use);
      plan.phase_references[ancestor.callee.value] = graph.ops[ancestor.call.value].first_use;
      plan.operations[ancestor.callee.value] = {EvaluationKind::kInline, graph.ops[ancestor.call.value].first_use, {}};
    }
    for (const auto prefix : selected.prefix) {
      budget.Step(); Require(!plan.phase_prefixes[prefix.value] && plan.operations[prefix.value].kind == EvaluationKind::kRoot);
      plan.phase_prefixes[prefix.value] = selected.branch;
    }
    if (const auto* arm = std::get_if<PhaseSelectionArm>(&selected.owner)) {
      Require(!selected.input && !selected.transport && !plan.phase_children[selected.branch.value]);
      plan.phase_children[selected.branch.value] = arm->parent;
      continue;
    }
    Require(selected.input && !plan.logical_values[selected.value.value]);
    plan.logical_values[selected.value.value] = selected.branch;
    const auto input = selected.transport ? graph.ops[selected.transport->value].first_use : selected.input;
    plan.operations[selected.branch.value] = {EvaluationKind::kLogicalValue, input, {}};
    if (selected.transport) {
      Require(!plan.phase_transports[selected.transport->value]);
      plan.phase_transports[selected.transport->value] = selected.input;
      plan.operations[selected.transport->value] = {EvaluationKind::kInline, selected.input, {}};
    }
  }
  plan.phase_selections = std::move(operands);
}
// A composition can expose another valid, still-uncommitted selection (for
// example after a checked member base becomes one expression). Discovery is
// not an obligation to migrate every candidate. Recheck exactly the sealed
// choices; the independent placement census below still rejects every owned
// logical branch, prefix or phi without its certificate.
static bool RetainSealedSelections(const EvaluationPlan& plan,
    std::vector<PhaseSelectionOperand>& candidates, TreeCheckBudget& budget) {
  std::size_t next = 0;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    budget.Step();
    if (next == plan.phase_selections.size()) break;
    const auto wanted = plan.phase_selections[next].branch;
    if (candidates[i].branch < wanted) continue;
    if (candidates[i].branch != wanted) return false;
    if (i != next) candidates[next] = std::move(candidates[i]);
    ++next;
  }
  if (next != plan.phase_selections.size()) return false;
  candidates.resize(next);
  return true;
}
// After phase composition: rerun the same proof on the composed plan. Accept
// only an identical result up to each selection's execution owner, which
// legitimately moves to the composed root; then adopt those owners.
bool RefreshPhaseSelectionExecutions(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  OperandSelections proof(graph, plan, budget, regions);
  auto operands = proof.CheckAll(regions);
  proof.Account();
  if (!RetainSealedSelections(plan, operands, budget)) return false;
  for (std::size_t i = 0; i < operands.size(); ++i) {
    // Field-wise, without copying the prefix/ancestor containers.
    const auto& a = operands[i]; const auto& b = plan.phase_selections[i];
    budget.Step(a.prefix.size() + a.ancestors.size() + 1);
    if (a.owner != b.owner || a.branch != b.branch || a.value != b.value || a.input != b.input ||
        a.transport != b.transport || a.prefix != b.prefix || a.ancestors != b.ancestors) return false;
  }
  for (std::size_t i = 0; i < operands.size(); ++i) { budget.Step(); plan.phase_selections[i].execution = operands[i].execution; }
  return true;
}
void VerifyPhaseSelectionOperands(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  Require(plan.logical_values.empty() || plan.logical_values.size() == graph.values.size());
  if (plan.phase_selections.empty()) {
    Require(plan.logical_values.empty() && plan.phase_transports.empty() && plan.phase_prefixes.empty() && plan.phase_references.empty() && plan.phase_children.empty());
    for (const auto& op : plan.operations) { budget.Step(); Require(op.kind != EvaluationKind::kLogicalValue); }
    return;
  }
  Require(!plan.logical_values.empty() && plan.phase_transports.size() == graph.ops.size() &&
      plan.phase_prefixes.size() == graph.ops.size() && plan.phase_references.size() == graph.ops.size() && plan.phase_children.size() == graph.ops.size());
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  OperandSelections proof(graph, plan, budget, regions);
  auto expected_operands = proof.CheckAll(regions);
  Require(RetainSealedSelections(plan, expected_operands, budget));
  enum class BranchPlacement : std::uint8_t { kNone, kValue, kArm };
  std::vector<BranchPlacement> branches;
  std::vector<std::uint8_t> values, transports, prefixes;
  budget.Array(values, graph.values.size());
  budget.Array(transports, graph.ops.size()); budget.Array(branches, graph.ops.size());
  budget.Array(prefixes, graph.ops.size());
  std::vector<std::uint8_t> references; budget.Array(references, graph.ops.size());
  for (std::size_t index = 0; index < expected_operands.size(); ++index) {
    budget.Step(); const auto* expected = &expected_operands[index];
    budget.Step(expected->prefix.size()); budget.Step(expected->ancestors.size());
    Require(plan.phase_selections[index] == *expected);
    const auto reference = [&](PhaseCallReference binding) {
      budget.Step(); const auto callee = binding.callee.value;
      Require(plan.phase_references[callee] == graph.ops[binding.call.value].first_use &&
          plan.operations[callee].kind == EvaluationKind::kInline &&
          plan.operations[callee].consumer == plan.phase_references[callee] && !plan.operations[callee].temporary);
      references[callee] = 1;
    };
    if (const auto* binding = std::get_if<PhaseCallReference>(&expected->owner)) reference(*binding);
    for (const auto& ancestor : expected->ancestors) reference(ancestor);
    const auto* arm = std::get_if<PhaseSelectionArm>(&expected->owner);
    if (!arm) {
      Require(!values[expected->value.value]); values[expected->value.value] = 1;
      Require(plan.logical_values[expected->value.value] == expected->branch && expected->input);
    }
    const auto& branch = plan.operations[expected->branch.value];
    const auto input = expected->transport ? graph.ops[expected->transport->value].first_use : expected->input;
    branches[expected->branch.value] = arm ? BranchPlacement::kArm : BranchPlacement::kValue;
    for (const auto prefix : expected->prefix) {
      budget.Step(); Require(!prefixes[prefix.value] && plan.phase_prefixes[prefix.value] == expected->branch &&
          plan.operations[prefix.value].kind == EvaluationKind::kRoot);
      prefixes[prefix.value] = 1;
    }
    if (expected->transport) {
      const auto op = expected->transport->value;
      Require(!transports[op] && plan.phase_transports[op] == expected->input &&
          plan.operations[op].kind == EvaluationKind::kInline &&
          plan.operations[op].consumer == expected->input && !plan.operations[op].temporary);
      transports[op] = 1;
    }
    if (arm) Require(plan.phase_children[expected->branch.value] == arm->parent &&
        !expected->input && !expected->transport && branch.kind == EvaluationKind::kRoot && !branch.consumer);
    else Require(!plan.phase_children[expected->branch.value] &&
        branch.kind == EvaluationKind::kLogicalValue && branch.consumer == input && !branch.temporary);
  }
  proof.Account();
  for (std::size_t i = 0; i < values.size(); ++i) { budget.Step(); Require(plan.logical_values[i].has_value() == (values[i] != 0)); }
  for (std::size_t i = 0; i < transports.size(); ++i) {
    budget.Step(); Require(plan.phase_transports[i].has_value() == (transports[i] != 0));
    Require(plan.phase_prefixes[i].has_value() == (prefixes[i] != 0));
    Require(plan.phase_references[i].has_value() == (references[i] != 0));
    Require((plan.operations[i].kind == EvaluationKind::kLogicalValue) == (branches[i] == BranchPlacement::kValue));
    Require(plan.phase_children[i].has_value() == (branches[i] == BranchPlacement::kArm));
  }
}
}  // namespace abcd::source
