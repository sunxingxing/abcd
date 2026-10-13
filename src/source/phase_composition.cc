#include "phase_composition.h"

#include <algorithm>

#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_corridor.h"
#include "phase_predicate_prefix.h"

namespace abcd::source {
// Defined with the selection proof: reruns its CheckAll on the composed plan.
bool RefreshPhaseSelectionExecutions(const analysis::ValueProgram&, EvaluationPlan&, TreeCheckBudget&);
namespace {
using namespace analysis;
void Require(bool condition) { if (!condition) throw EvaluationError::kOrder; }
// One event of the expected source order: an ordered operation, or a whole
// committed selection identified by its original branch.
struct Item {
  bool selection = false;
  OpId id;
};
class Composition {
 public:
  Composition(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget)
      : graph_(graph), plan_(plan), budget_(budget), phase_budget_(budget.Remaining()),
        corridors_(graph, plan, phase_budget_) {
    budget.Admit(phase_budget_.Cost().bytes, 1);
    accounted_work_ = phase_budget_.Cost().work; budget.Step(accounted_work_);
    regions_ = FindPhasedPhiSelections(graph, plan, budget);
    roots_ = IndexPhaseInlineRoots(graph, plan, budget);
    budget.Array(region_of_, graph.ops.size());
    for (std::size_t i = 0; i < regions_.size(); ++i) { budget.Step(); region_of_[regions_[i].branch.value] = i; }
    budget.Array(incoming_, graph.blocks.size()); budget.Array(outgoing_, graph.blocks.size());
    budget.Array(predecessor_, graph.blocks.size()); budget.Array(copied_, graph.edges.size());
    budget.Array(visited_, graph.blocks.size());
    for (std::size_t e = 0; e < graph.edges.size(); ++e) {
      budget.Step(); const auto& edge = graph.edges[e];
      if (edge.kind == FlowEdgeKind::kException) continue;
      ++incoming_[edge.target.value]; ++outgoing_[edge.source.value];
      predecessor_[edge.target.value] = EdgeId{static_cast<std::uint32_t>(e)};
    }
    if (plan.phis) for (const auto& bundle : plan.phis->bundles) {
      budget.Step(); if (const auto* edge = std::get_if<EdgeId>(&bundle.incoming)) copied_[edge->value] |= !bundle.schedule.steps.empty();
    }
    // Literal intervals, guarded module pairs and phased object operands own
    // other composite events; this first increment leaves those trees alone.
    budget.Array(excluded_, graph.ops.size());
    if (plan.literals) for (const auto& literal : plan.literals->objects) {
      budget.Step(); if (literal.allocation.value < excluded_.size()) excluded_[literal.allocation.value] = 1;
    }
    for (const auto& module : plan.modules) {
      budget.Step(); excluded_[module.pair.read.value] = 1;
      if (module.pair.guard) excluded_[module.pair.guard->value] = 1;
    }
    for (std::size_t i = 0; i < plan.logical_objects.size(); ++i) { budget.Step(); if (plan.logical_objects[i]) excluded_[i] = 1; }
    // Events owned by a committed selection: its retained prefix roots and the
    // inline predicate chain of its branch, each with its own inline subtree.
    budget.Array(owner_, graph.ops.size()); budget.Array(owned_count_, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const auto root = roots_[i];
      std::optional<OpId> owner;
      if (root.value < plan.phase_prefixes.size() && plan.phase_prefixes[root.value]) owner = plan.phase_prefixes[root.value];
      else if (region_of_[root.value] && plan.operations[root.value].kind == EvaluationKind::kLogicalValue) owner = root;
      if (!owner || !PhaseCorridors::Event(graph.ops[i], plan.operations[i])) continue;
      owner_[i] = owner; ++owned_count_[owner->value];
    }
    budget.Array(overlay_, graph.ops.size()); budget.Array(member_, graph.ops.size());
  }
  // Expands root's final-ownership tree, optionally adding eligible root
  // producers, then proves the actual order and catch obligations.
  bool Prove(OpId root, bool tentative) {
    items_.clear(); nodes_.clear(); added_.clear();
    try {
      Expand(root, tentative);
      Require(std::any_of(items_.begin(), items_.end(), [](const Item& item) { return item.selection; }));
      Require(!tentative || !added_.empty());
      Walk(root);
      return true;
    } catch (EvaluationError) {
      for (const auto& [producer, use] : added_) { budget_.Step(); overlay_[producer.value].reset(); }
      added_.clear();
      return false;
    }
  }
  [[nodiscard]] std::span<const std::pair<OpId, UseId>> Added() const { return added_; }
  [[nodiscard]] std::span<const OpId> Nodes() const { return nodes_; }
  void Claim() { for (const auto node : nodes_) { budget_.Step(); member_[node.value] = 1; } }
  [[nodiscard]] bool Member(OpId op) const { return member_[op.value] != 0; }
  void Account() { budget_.Step(phase_budget_.Cost().work - accounted_work_); accounted_work_ = phase_budget_.Cost().work; }

 private:
  OpEvaluation Placement(OpId op) const {
    if (overlay_[op.value]) return {EvaluationKind::kInline, overlay_[op.value], {}};
    return plan_.operations[op.value];
  }
  std::optional<OpId> Selected(UseId use, ValueId value) const {
    if (value.value >= plan_.logical_values.size() || !plan_.logical_values[value.value]) return {};
    const auto branch = *plan_.logical_values[value.value];
    const auto& placement = plan_.operations[branch.value];
    if (placement.kind != EvaluationKind::kLogicalValue || placement.consumer != use ||
        !region_of_[branch.value] || (branch.value < plan_.phase_children.size() && plan_.phase_children[branch.value]))
      return {};
    return branch;
  }
  bool Eligible(OpId producer, OpId consumer, UseId use) const {
    const auto& child = graph_.ops[producer.value];
    const auto& parent = graph_.ops[consumer.value];
    const auto& occurrence = graph_.uses[use.value];
    const auto& placement = plan_.operations[producer.value];
    if (producer.value >= consumer.value || child.life != ValueLife::kLive || !child.result ||
        placement.kind != EvaluationKind::kRoot || placement.temporary == TemporaryReason::kMultipleUses ||
        placement.temporary == TemporaryReason::kPredicateSnapshot || excluded_[producer.value] ||
        region_of_[producer.value] || std::holds_alternative<ValueCopy>(child.operation) ||
        (producer.value < plan_.phase_prefixes.size() && plan_.phase_prefixes[producer.value]) ||
        (producer.value < plan_.phase_references.size() && plan_.phase_references[producer.value]) ||
        (producer.value < plan_.phase_transports.size() && plan_.phase_transports[producer.value]) ||
        !corridors_.SoleUse(*child.result, use)) return false;
    const auto shape = DescribeEvaluation(parent.operation);
    if (!shape || shape->NeedsSnapshot(occurrence.role) || occurrence.role == OperandRole::kReceiver ||
        PredicateSnapshot(graph_, parent, occurrence.operand) ||
        (!PhaseCorridors::Expression(parent.operation) && !PhaseCorridors::ConsumesOperands(parent.operation))) return false;
    if (PhaseCorridors::Expression(child.operation)) return true;
    // Closed pure values are admitted by the shared evaluation semantics.
    const auto produced = DescribeEvaluation(child.operation);
    return produced && produced->expression && produced->inputs == InputEvaluation::kEager &&
        produced->Count() == 0 && child.use_count == 0 && child.normal == LanguageEffect::kNone &&
        child.before_throw == LanguageEffect::kNone && child.exceptional == ExceptionalCompletion::kImpossible &&
        graph_.values[child.result->value].domain.IsLanguage();
  }
  void Expand(OpId root, bool tentative) {
    struct Frame { OpId op; std::uint32_t ordinal = 0; };
    std::vector<Frame> stack;
    Require(!excluded_[root.value] && !member_[root.value]);
    stack.push_back({root});
    while (!stack.empty()) {
      budget_.Step(); Require(stack.size() <= graph_.ops.size());
      auto& frame = stack.back();
      const auto& op = graph_.ops[frame.op.value];
      if (frame.ordinal == op.use_count) {
        const auto id = frame.op; stack.pop_back();
        nodes_.push_back(id);
        if (PhaseCorridors::Event(graph_.ops[id.value], Placement(id))) items_.push_back({false, id});
        continue;
      }
      const UseId use{op.first_use.value + frame.ordinal++};
      const auto consumer = frame.op;
      const auto& occurrence = graph_.uses[use.value];
      if (IsMemberReceiver(graph_, plan_, occurrence)) continue;
      const auto operand = ResolveEvaluationOperand(plan_, occurrence.operand);
      const auto* value = std::get_if<ValueId>(&operand);
      if (!value) continue;
      if (const auto branch = Selected(use, *value)) { items_.push_back({true, *branch}); continue; }
      const auto* result = std::get_if<OpResult>(&graph_.values[value->value].origin);
      if (!result) continue;
      const auto producer = result->op;
      const auto placement = Placement(producer);
      if (placement.kind == EvaluationKind::kInline && placement.consumer == use) {
        // Callee references certified by the selection proof keep their own
        // evidence; composite literal/module/object events are not expanded.
        Require(!excluded_[producer.value] && !member_[producer.value] &&
            (producer.value >= plan_.phase_references.size() || !plan_.phase_references[producer.value]));
        stack.push_back({producer});
      } else if (tentative && Eligible(producer, consumer, use)) {
        overlay_[producer.value] = use; added_.push_back({producer, use});
        stack.push_back({producer});
      }
    }
  }
  void Census(OpId op, OpId root) {
    budget_.Step();
    if (op == root || graph_.ops[op.value].exceptional != ExceptionalCompletion::kMayThrow) return;
    // A moved throw point keeps the root's handler and needs no new staging.
    Require(corridors_.SameCatch(op, root) && !corridors_.HasCatchCopies(op));
  }
  void CensusRegion(const SelectionRegion& region, OpId root) {
    ++stamp_;
    std::vector<WorkBlockId> queue;
    const auto& header = graph_.blocks[region.header.value];
    for (std::uint32_t e = 0; e < header.edge_count; ++e) {
      budget_.Step(); const auto& edge = graph_.edges[header.first_edge.value + e];
      if (edge.kind != FlowEdgeKind::kException) queue.push_back(edge.target);
    }
    for (std::size_t at = 0; at < queue.size(); ++at) {
      budget_.Step(); const auto block = queue[at];
      if (block == region.join || visited_[block.value] == stamp_) continue;
      visited_[block.value] = stamp_;
      Require(queue.size() <= graph_.edges.size() + 2);
      const auto& work = graph_.blocks[block.value];
      for (std::uint32_t i = 0; i < work.op_count; ++i) {
        const OpId id{work.first_op.value + i};
        if (graph_.ops[id.value].life == ValueLife::kLive) Census(id, root);
      }
      bool exits = false;
      for (std::uint32_t e = 0; e < work.edge_count; ++e) {
        budget_.Step(); const auto& edge = graph_.edges[work.first_edge.value + e];
        if (edge.kind == FlowEdgeKind::kException) continue;
        exits = true; queue.push_back(edge.target);
      }
      // Every normal path of a committed selection completes at its join.
      Require(exits);
    }
  }
  // Walks the actual normal path backwards from the root: every ordered event
  // must be the next expected item, every edge single-entry and copy-free, and
  // each selection is crossed only from its own join to its own header.
  void Walk(OpId root) {
    auto k = items_.size();
    std::optional<OpId> selection;
    std::size_t remaining = 0;
    auto block = graph_.ops[root.value].block;
    auto end = root.value - graph_.blocks[block.value].first_op.value + 1;
    for (std::size_t steps = 0; steps <= graph_.blocks.size() + graph_.ops.size(); ++steps) {
      budget_.Step();
      const auto& work = graph_.blocks[block.value];
      for (auto i = end; i-- > 0;) {
        budget_.Step(); const OpId id{work.first_op.value + i};
        if (graph_.ops[id.value].life != ValueLife::kLive || !PhaseCorridors::Event(graph_.ops[id.value], Placement(id))) continue;
        if (remaining) {
          Require(owner_[id.value] == selection); --remaining;
        } else {
          Require(k && !items_[k - 1].selection && items_[k - 1].id == id); --k;
        }
        Census(id, root);
        if (!k && !remaining) return;
      }
      if (!k && !remaining) return;
      Require(graph_.phases[block.value].phase != ExceptionPhase::kCatch);
      if (!remaining && k && items_[k - 1].selection) {
        const auto& region = regions_[*region_of_[items_[k - 1].id.value]];
        if (region.join == block) {
          // The join's normal entries are exactly this selection's two arms.
          Require(region.keep_input && region.evaluate_input && *region.keep_input != *region.evaluate_input &&
              graph_.edges[region.keep_input->value].target == block &&
              graph_.edges[region.evaluate_input->value].target == block && incoming_[block.value] == 2);
          CensusRegion(region, root);
          selection = items_[k - 1].id; remaining = owned_count_[selection->value]; --k;
          if (!k && !remaining) return;
          block = region.header; end = graph_.blocks[block.value].op_count;
          continue;
        }
      }
      // An ordinary step: one normal entry, copy-free, whose source always
      // continues here. Exceptional edges are covered by the catch census.
      Require(incoming_[block.value] == 1 && predecessor_[block.value]);
      const auto edge = *predecessor_[block.value];
      const auto source = graph_.edges[edge.value].source;
      Require(!copied_[edge.value] && outgoing_[source.value] == 1);
      block = source; end = graph_.blocks[block.value].op_count;
    }
    Require(false);
  }

  const ValueProgram& graph_;
  const EvaluationPlan& plan_;
  TreeCheckBudget& budget_;
  PhaseBudget phase_budget_;
  PhaseCorridors corridors_;
  std::uint64_t accounted_work_ = 0;
  std::vector<SelectionRegion> regions_;
  std::vector<OpId> roots_;
  std::vector<std::optional<std::size_t>> region_of_;
  std::vector<std::uint32_t> incoming_, outgoing_, visited_;
  std::vector<std::optional<EdgeId>> predecessor_;
  std::vector<std::uint8_t> copied_, excluded_, member_;
  std::vector<std::optional<OpId>> owner_;
  std::vector<std::size_t> owned_count_;
  std::vector<std::optional<UseId>> overlay_;
  std::vector<Item> items_;
  std::vector<OpId> nodes_;
  std::vector<std::pair<OpId, UseId>> added_;
  std::uint32_t stamp_ = 0;
};
bool StatementRoot(const ValueOp& op, const OpEvaluation& placement) {
  return op.life == ValueLife::kLive &&
      (placement.kind == EvaluationKind::kRoot || placement.kind == EvaluationKind::kPhiInput) &&
      (PhaseCorridors::Expression(op.operation) || PhaseCorridors::ConsumesOperands(op.operation));
}
}  // namespace
void ComposePhaseExpressions(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  if (plan.flow != EvaluationFlow::kPhased || plan.phase_selections.empty()) return;
  struct Edge { OpId producer; UseId use; OpEvaluation original; };
  std::vector<Edge> committed;
  std::vector<OpId> roots;
  {
    Composition proof(graph, plan, budget);
    // Later roots first: an absorbed root is never proposed as its own tree.
    // Committed edges stay visible to later proofs through the overlay.
    for (std::size_t i = graph.ops.size(); i-- > 0;) {
      budget.Step(); const OpId id{static_cast<std::uint32_t>(i)};
      if (proof.Member(id) || !StatementRoot(graph.ops[i], plan.operations[i]) || !proof.Prove(id, true)) continue;
      for (const auto& [producer, use] : proof.Added()) {
        budget.Step(); committed.push_back({producer, use, plan.operations[producer.value]});
      }
      proof.Claim(); roots.push_back(id);
    }
    proof.Account();
  }
  if (roots.empty()) return;
  for (const auto& edge : committed) {
    budget.Step(); plan.operations[edge.producer.value] = {EvaluationKind::kInline, edge.use, {}};
  }
  std::reverse(roots.begin(), roots.end());
  plan.phase_compositions = std::move(roots);
  // Conservative gate: the selection proof must accept exactly the same
  // regions, owners, prefixes and references; only execution may change.
  if (RefreshPhaseSelectionExecutions(graph, plan, budget)) return;
  for (const auto& edge : committed) { budget.Step(); plan.operations[edge.producer.value] = edge.original; }
  plan.phase_compositions.clear();
}
std::vector<std::uint8_t> VerifyPhaseCompositions(const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget) {
  std::vector<std::uint8_t> mask;
  if (plan.phase_compositions.empty()) return mask;
  Require(plan.flow == EvaluationFlow::kPhased && !plan.phase_selections.empty());
  budget.Array(mask, graph.ops.size());
  Composition proof(graph, plan, budget);
  for (std::size_t i = 0; i < plan.phase_compositions.size(); ++i) {
    budget.Step(); const auto root = plan.phase_compositions[i];
    Require(root.value < graph.ops.size() && (!i || plan.phase_compositions[i - 1] < root) &&
        StatementRoot(graph.ops[root.value], plan.operations[root.value]));
    Require(proof.Prove(root, false));
    for (const auto node : proof.Nodes()) { budget.Step(); Require(!mask[node.value]); mask[node.value] = 1; }
    proof.Claim();
  }
  proof.Account();
  return mask;
}
}  // namespace abcd::source
