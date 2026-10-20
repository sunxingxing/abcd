#include "phase_composition.h"

#include <algorithm>

#include "assignment_values.h"
#include "call_plan.h"
#include "checked_read_event.h"
#include "literal_reference_view.h"
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
// A backward scan position: ops [0, end) of block remain to be examined.
struct Cursor {
  WorkBlockId block;
  std::uint32_t end = 0;
};
// The first thing met when scanning backwards along single-entry, copy-free
// normal steps: an ordered event, the start of a committed selection's join,
// or anything else (a merge, a copy, a catch block), which blocks composition.
struct Stop {
  enum class Kind : std::uint8_t { kBlocked, kEvent, kJoin } kind = Kind::kBlocked;
  OpId op;
  std::size_t region = 0;
  Cursor after;
};
struct Frame {
  OpId op;
  std::uint32_t ordinal = 0;
};
enum class Mode : std::uint8_t { kBuild, kVerify };
class Composition {
 public:
  Composition(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget, bool checked = false)
      : graph_(graph), plan_(plan), budget_(budget), phase_budget_(budget.Remaining()),
        corridors_(graph, plan, phase_budget_, PhaseLiteralIntervals::kInclude,
            checked ? CheckedReadFusion::kCompose : CheckedReadFusion::kStandalone), checked_mode_(checked) {
    budget.Admit(phase_budget_.Cost().bytes, 1);
    accounted_work_ = phase_budget_.Cost().work; budget.Step(accounted_work_);
    regions_ = FindPhasedPhiSelections(graph, plan, budget);
    if (!checked)
      literal_atoms_ =
          IndexLiteralReferences(graph, plan, budget, &literal_members_);
    const auto roots = IndexPhaseInlineRoots(graph, plan, budget);
    budget.Array(region_of_, graph.ops.size()); budget.Array(join_of_, graph.blocks.size());
    budget.Array(shared_join_, graph.blocks.size());
    for (std::size_t i = 0; i < regions_.size(); ++i) {
      budget.Step(); const auto branch = regions_[i].branch;
      region_of_[branch.value] = i;
      if (!Committed(branch)) continue;
      auto& join = join_of_[regions_[i].join.value];
      if (join) shared_join_[regions_[i].join.value] = 1;
      join = i;
    }
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
    // Literal intervals and phased object operands retain their own proofs.
    // Checked-call composition can consume an already-inline module read's
    // verified pair as an atom; it never changes module-read admission.
    budget.Array(excluded_, graph.ops.size());
    if (plan.literals) for (const auto& literal : plan.literals->objects) {
      budget.Step(); if (literal.allocation.value < excluded_.size()) excluded_[literal.allocation.value] = 1;
    }
    for (const auto& pair : plan.guarded_reads) { budget.Step(); excluded_[pair.read.value] = !checked_mode_; excluded_[pair.guard.value] = 1; }
    budget.Array(module_ends_, checked_mode_ && !plan.modules.empty() ? graph.ops.size() : 0);
    for (const auto& module : plan.modules) {
      budget.Step();
      const bool retained = checked_mode_ && plan.operations[module.pair.read.value].kind == EvaluationKind::kInline;
      excluded_[module.pair.read.value] = !retained;
      if (retained) module_ends_[module.pair.read.value] = module.pair.guard;
      if (module.pair.guard) excluded_[module.pair.guard->value] = 1;
    }
    for (std::size_t i = 0; i < plan.logical_objects.size(); ++i) { budget.Step(); if (plan.logical_objects[i]) excluded_[i] = 1; }
    // Events owned by a committed selection: its retained prefix roots and the
    // inline predicate chain of its branch, each with its own inline subtree.
    budget.Array(owner_, graph.ops.size()); budget.Array(owned_count_, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const auto root = roots[i];
      std::optional<OpId> owner;
      if (root.value < plan.phase_prefixes.size() && plan.phase_prefixes[root.value]) owner = plan.phase_prefixes[root.value];
      else if (region_of_[root.value] && Committed(root)) owner = root;
      if (!owner || !Event(OpId{static_cast<std::uint32_t>(i)})) continue;
      owner_[i] = owner; ++owned_count_[owner->value];
    }
    budget.Array(latest_, graph.ops.size());
    for (const auto& work : graph.blocks) {
      std::optional<std::uint32_t> latest;
      for (std::uint32_t i = 0; i < work.op_count; ++i) {
        budget.Step(); const auto& item = graph.ops[work.first_op.value + i];
        if (item.life == ValueLife::kLive && Event(OpId{work.first_op.value + i})) latest = i;
        latest_[work.first_op.value + i] = latest;
      }
    }
    budget.Array(entry_state_, graph.blocks.size()); budget.Array(entry_stop_, graph.blocks.size());
    budget.Admit(graph.blocks.size(), sizeof(WorkBlockId)); chain_.reserve(graph.blocks.size());
    budget.Admit(chain_.capacity() - graph.blocks.size(), sizeof(WorkBlockId));
    budget.Array(reached_, graph.ops.size());
    budget.Array(overlay_, graph.ops.size()); budget.Array(added_, graph.ops.size());
    budget.Array(valid_, graph.ops.size()); budget.Array(events_, graph.ops.size());
    budget.Array(selection_, graph.ops.size()); budget.Array(fresh_, graph.ops.size());
    budget.Array(checked_tree_, checked_mode_ ? graph.ops.size() : 0);
    budget.Array(repaired_, checked_mode_ ? graph.ops.size() : 0);
    budget.Array(seed_, checked_mode_ ? graph.ops.size() : 0);
    budget.Array(checked_source_, checked_mode_ ? graph.ops.size() : 0);
    budget.Array(failures_, checked_mode_ ? graph.ops.size() : 0);
    if (checked_mode_) for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const OpId call{static_cast<std::uint32_t>(i)};
      if (const auto property = MemberProperty(graph, plan, call);
          property && !MemberReferenceInline(graph, plan, call, *property)) seed_[i] = property;
    }
    budget.Array(begin_, graph.ops.size()); budget.Array(last_, graph.ops.size());
    budget.Array(region_state_, regions_.size()); budget.Array(region_throw_, regions_.size());
    budget.Admit(graph.ops.size(), sizeof(Frame)); stack_.reserve(graph.ops.size());
    budget.Admit(stack_.capacity() - graph.ops.size(), sizeof(Frame));
    budget.Admit(graph.ops.size(), sizeof(OpId)); nodes_.reserve(graph.ops.size());
    budget.Admit(nodes_.capacity() - graph.ops.size(), sizeof(OpId));
    budget.Admit(graph.edges.size() + 2, sizeof(WorkBlockId)); queue_.reserve(graph.edges.size() + 2);
    budget.Admit(queue_.capacity() - graph.edges.size() - 2, sizeof(WorkBlockId));
  }
  // Composes one node over its operands in reverse source order. Existing
  // owned children and committed selections must fit; in kBuild an eligible
  // root child is absorbed when it fits, otherwise it keeps its snapshot.
  // Returns whether the node has a proved span (kVerify throws instead).
  bool Compose(OpId consumer, Mode mode) {
    budget_.Step();
    const auto& op = graph_.ops[consumer.value];
    const bool may_throw = EvaluationMayThrow(graph_, plan_, consumer);
    const auto fail = [&]() -> bool {
      Require(mode == Mode::kBuild);
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        budget_.Step(); const UseId use{op.first_use.value + ordinal};
        const auto link = Edge(consumer, use, false);  // Canonical, alias-resolved operand.
        if (link.producer && added_[link.producer->value]) Undo(*link.producer);
      }
      return false;
    };
    const auto module_end = module_ends_.empty() ? std::optional<OpId>{} : module_ends_[consumer.value];
    if (checked_mode_ && (GuardedInput(plan_, consumer) || module_end)) {
      // Consume BOTH original events. Next proves execution count and no
      // intervening events/copies, independently of numeric OpId/PC adjacency.
      if (!module_end) checked_source_[consumer.value] = consumer;
      const auto guard = module_end.value_or(GuardedPeer(plan_, consumer).value_or(consumer));
      if (module_end && (!corridors_.SameCatch(consumer, guard) || corridors_.HasCatchCopies(guard))) return fail();
      const auto& end = graph_.ops[guard.value];
      const auto preceding = Next({end.block, end.order});
      if (preceding.kind != Stop::Kind::kEvent || preceding.op != consumer) return fail();
      valid_[consumer.value] = events_[consumer.value] = 1; checked_tree_[consumer.value] = !module_end;
      last_[consumer.value] = guard; begin_[consumer.value] = preceding.after;
      return true;
    }
    if (const auto* mutation=MemberUpdateAt(plan_,consumer)) {
      valid_[consumer.value]=events_[consumer.value]=selection_[consumer.value]=1;
      begin_[consumer.value]={graph_.ops[mutation->first.value].block,graph_.ops[mutation->first.value].order};
      last_[consumer.value]=consumer;return true;
    }
    if(MemberUpdateIntrinsic(plan_,consumer))return fail();
    if (const auto* atom = LiteralAtom(consumer)) {
      // Separate verified forest boundary. The ordinary allocation exclusion
      // below is unchanged: this span includes every conditional arm, Commit
      // and exception, independently rederived by IndexLiteralReferences.
      valid_[consumer.value] = events_[consumer.value] =
          selection_[consumer.value] = 1;
      begin_[consumer.value] = {op.block, op.order};
      last_[consumer.value] = atom->last;
      return true;
    }
    if (op.life != ValueLife::kLive || excluded_[consumer.value] ||
        (!PhaseCorridors::Expression(op.operation) && !PhaseCorridors::ConsumesOperands(op.operation))) return fail();
    // A moved throw point keeps this consumer's handler and needs no staging.
    const auto fits_catch = [&](OpId thrower) {
      return may_throw && corridors_.SameCatch(thrower, consumer) && !corridors_.HasCatchCopies(thrower);
    };
    auto cursor = Cursor{op.block, consumer.value - graph_.blocks[op.block.value].first_op.value};
    auto stop = Next(cursor);
    std::optional<OpId> last;
    bool events = false, selection = false, fresh = false, checked = false, repaired = false, checked_argument = false;
    for (std::uint32_t ordinal = op.use_count; ordinal != 0; --ordinal) {
      budget_.Step(); const UseId use{op.first_use.value + ordinal - 1};
      const auto link = Edge(consumer, use, mode == Mode::kBuild);
      if (checked_mode_ && !link.producer) {
        // Retain the rejected occurrence too: uniqueness/reference failures
        // must not lose their read/guard identity just because no edge was proposed.
        if (const auto producer = EvaluationProducer(graph_, plan_, use);
            producer && checked_source_[producer->value]) {
          const auto read = *checked_source_[producer->value];
          checked_source_[consumer.value] = read;
          failures_[consumer.value] = GuardedCallFailure{GuardedCallError::kUse, consumer, read,
              *GuardedPeer(plan_, read), use};
        }
      }
      if (link.selection) {
        const auto& region = regions_[*region_of_[link.selection->value]];
        if (stop.kind != Stop::Kind::kJoin || stop.region != *region_of_[link.selection->value]) return fail();
        // The join's normal entries are exactly this selection's two arms.
        if (!region.keep_input || !region.evaluate_input || *region.keep_input == *region.evaluate_input ||
            graph_.edges[region.keep_input->value].target != region.join ||
            graph_.edges[region.evaluate_input->value].target != region.join ||
            incoming_[region.join.value] != 2) return fail();
        const auto thrower = RegionThrower(*region_of_[link.selection->value]);
        if (!thrower || (*thrower && !fits_catch(**thrower))) return fail();
        auto inner = Cursor{region.header, graph_.blocks[region.header.value].op_count};
        for (std::size_t owned = 0; owned < owned_count_[link.selection->value]; ++owned) {
          const auto event = Next(inner);
          if (event.kind != Stop::Kind::kEvent || owner_[event.op.value] != *link.selection ||
              (EvaluationMayThrow(graph_, plan_, event.op) && !fits_catch(event.op))) return fail();
          inner = event.after;
        }
        if (!last) last = *link.selection;
        events = selection = true; cursor = inner; stop = Next(cursor);
        continue;
      }
      if (!link.producer) continue;
      const auto producer = *link.producer;
      if (checked_mode_ && checked_source_[producer.value]) {
        checked_source_[consumer.value] = checked_source_[producer.value];
        if (failures_[producer.value]) failures_[consumer.value] = failures_[producer.value];
        else {
          const auto read = *checked_source_[producer.value];
          failures_[consumer.value] = GuardedCallFailure{GuardedCallError::kOrder, consumer, read,
              *GuardedPeer(plan_, read), use};
        }
      }
      if ((!MemberUpdateAt(plan_,producer) && !LiteralAtom(producer) && excluded_[producer.value]) ||
          Owned(producer) || !valid_[producer.value] ||
          (!MemberUpdateAt(plan_,producer) && !LiteralAtom(producer) && !Transport(producer) &&
           !Fusable(producer, consumer, use))) {
        if (link.added) continue;  // An eligible root keeps its snapshot.
        return fail();
      }
      // Invariant of a composed node: each absorbed throw point has no catch
      // copies and the node's handler, and a node that cannot throw absorbed
      // none. Checking the child itself therefore covers its whole subtree.
      bool fits = !events_[producer.value] ||
          (stop.kind == Stop::Kind::kEvent && last_[producer.value] && stop.op == *last_[producer.value]);
      if (fits && (LiteralAtom(producer)
                       ? LiteralAtom(producer)->may_throw
                       : EvaluationMayThrow(graph_, plan_, producer))) {
        fits = fits_catch(producer);
        if (!fits && checked_mode_ && failures_[consumer.value]) failures_[consumer.value]->kind = GuardedCallError::kCatchState;
      }
      if (!fits) {
        if (link.added) continue;
        return fail();
      }
      if (checked_mode_) {
        checked |= checked_tree_[producer.value] != 0;
        repaired |= repaired_[producer.value] != 0;
        if (graph_.uses[use.value].role == OperandRole::kArgument) checked_argument |= checked_tree_[producer.value] != 0;
      }
      if (link.added) { overlay_[producer.value] = use; added_[producer.value] = 1; fresh = true; }
      fresh |= fresh_[producer.value] != 0; selection |= selection_[producer.value] != 0;
      if (events_[producer.value]) {
        if (!last) last = last_[producer.value];
        events = true; cursor = begin_[producer.value]; stop = Next(cursor);
      }
    }
    if (Event(consumer)) { last = consumer; events = true; }
    if (checked_mode_) {
      const auto property = mode == Mode::kBuild ? seed_[consumer.value] : MemberProperty(graph_, plan_, consumer);
      if (property && checked_argument) {
        const auto placement = Placement(*property);
        repaired |= placement.kind == EvaluationKind::kInline && placement.consumer == op.first_use;
      }
      checked_tree_[consumer.value] = checked; repaired_[consumer.value] = repaired;
    }
    valid_[consumer.value] = 1; events_[consumer.value] = events; selection_[consumer.value] = selection;
    fresh_[consumer.value] = fresh; begin_[consumer.value] = cursor; last_[consumer.value] = last;
    return true;
  }
  // Builder: every node once in increasing order (producers precede their
  // consumers), then roots decide: a fresh tree with a selection is kept;
  // otherwise only the root's own new edges are withdrawn and its children are
  // reconsidered as roots. Linear in the method.
  std::vector<OpId> Build(std::vector<std::pair<OpId, UseId>>& edges) {
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) { budget_.Step(); Compose(OpId{static_cast<std::uint32_t>(i)}, Mode::kBuild); }
    std::vector<OpId> roots;
    budget_.Admit(graph_.ops.size(), sizeof(OpId)); roots.reserve(graph_.ops.size());
    budget_.Admit(roots.capacity() - graph_.ops.size(), sizeof(OpId));
    for (std::size_t i = graph_.ops.size(); i-- > 0;) {
      budget_.Step(); const OpId id{static_cast<std::uint32_t>(i)};
      // Every final root decides, valid or not: a failed root may still hold
      // new edges below existing inline edges of its tree.
      if (Placement(id).kind == EvaluationKind::kInline) continue;
      if (valid_[i] && fresh_[i] && (checked_mode_ ? checked_tree_[i] && repaired_[i] : selection_[i]) && StatementRoot(id)) { roots.push_back(id); continue; }
      Withdraw(id);
    }
    // Closure: every surviving new edge is reachable from a registered root.
    for (const auto root : roots) Reach(root);
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); if (added_[i] && !reached_[i]) Undo(OpId{static_cast<std::uint32_t>(i)});
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); if (added_[i]) edges.push_back({OpId{static_cast<std::uint32_t>(i)}, *overlay_[i]});
    }
    std::reverse(roots.begin(), roots.end());
    return roots;
  }
  // Verifier: expands the listed root's actual tree, then requires every node
  // to compose with all of its owned children. Returns the tree's members.
  std::span<const OpId> Verify(OpId root) {
    Require(root.value < graph_.ops.size() && StatementRoot(root) && !valid_[root.value]);
    nodes_.clear(); stack_.clear(); stack_.push_back({root});
    while (!stack_.empty()) {
      budget_.Step();
      auto& frame = stack_.back();
      const auto& op = graph_.ops[frame.op.value];
      if (MemberUpdateAt(plan_,frame.op)) frame.ordinal=op.use_count;
      if (frame.ordinal == op.use_count) {
        Require(nodes_.size() < nodes_.capacity()); nodes_.push_back(frame.op); stack_.pop_back();
        continue;
      }
      const UseId use{op.first_use.value + frame.ordinal++};
      const auto link = Edge(frame.op, use, false);
      if (!link.producer) continue;
      Require(!valid_[link.producer->value] && stack_.size() < stack_.capacity());
      stack_.push_back({*link.producer});
    }
    for (const auto node : nodes_) Compose(node, Mode::kVerify);
    Require(checked_mode_ ? checked_tree_[root.value] && repaired_[root.value] : selection_[root.value] != 0);
    return nodes_;
  }
  void Failures(std::vector<GuardedCallFailure>& output) {
    if (!checked_mode_) return;
    std::size_t count = 0;
    const auto failed = [&](std::size_t i) {
      return seed_[i] && failures_[i] && !(Placement(*seed_[i]).kind == EvaluationKind::kInline &&
          Placement(*seed_[i]).consumer == graph_.ops[i].first_use);
    };
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) { budget_.Step(); count += failed(i); }
    budget_.Admit(count, sizeof(GuardedCallFailure)); output.reserve(count);
    budget_.Admit(output.capacity() - count, sizeof(GuardedCallFailure));
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step(); if (!failed(i)) continue;
      auto failure = *failures_[i]; failure.call = OpId{static_cast<std::uint32_t>(i)};
      const auto& read = graph_.origins[graph_.ops[failure.read.value].origin.value];
      const auto& guard = graph_.origins[graph_.ops[failure.guard.value].origin.value];
      failure.read_pc = read.original.value_or(read.pc); failure.read_working_pc = read.pc;
      failure.guard_pc = guard.original.value_or(guard.pc); failure.guard_working_pc = guard.pc;
      output.push_back(failure);
    }
  }
  void MaskLiteralMembers(std::vector<std::uint8_t>& mask) {
    // Only forests reached from independently verified composition roots may
    // replace interval equations. Include their full original event membership,
    // especially implicit definition-base uses, not just the allocation token.
    for(std::size_t i=0;i<plan_.member_updates.owned.size();++i){budget_.Step();if(plan_.member_updates.owned[i]&&mask[plan_.member_updates.owned[i]->value])mask[i]=1;}
    for (std::size_t i = 0; i < literal_members_.size(); ++i) {
      budget_.Step();
      const auto owner = literal_members_[i];
      if (!owner || !mask[owner->value]) continue;
      Require(!mask[i] || i == owner->value);
      mask[i] = 1;
    }
  }
  void Account() { budget_.Step(phase_budget_.Cost().work - accounted_work_); accounted_work_ = phase_budget_.Cost().work; }

 private:
  bool Event(OpId id) const {
    const auto& op = graph_.ops[id.value];
    const auto shape = DescribeEvaluation(op.operation);
    // Input-free normal control contributes an edge, not a source effect.
    // Entry still proves that the traversed edge is single-entry, unconditional
    // and copy-free. This is not an opcode/adjacency exception for a split block.
    if (checked_mode_ && shape && shape->control == ControlEvaluation::kNormal &&
        shape->Count() == 0 && !op.result && op.normal == LanguageEffect::kNone &&
        op.before_throw == LanguageEffect::kNone && !EvaluationMayThrow(graph_, plan_, id)) return false;
    return PhaseCorridors::Event(op, plan_.operations[id.value]);
  }
  // An unregistered root keeps its original placements: walk its tree through
  // existing edges and undo every new edge met. Each child cut off this way is
  // a root again (a smaller OpId), decided later by the same loop.
  void Withdraw(OpId root) {
    stack_.clear(); stack_.push_back({root});
    while (!stack_.empty()) {
      budget_.Step(); const auto node = stack_.back().op; stack_.pop_back();
      const auto& op = graph_.ops[node.value];
      if(MemberUpdateAt(plan_,node))continue;
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        budget_.Step(); const UseId use{op.first_use.value + ordinal};
        const auto link = Edge(node, use, false);
        if (!link.producer) continue;
        const auto producer = *link.producer;
        if (added_[producer.value]) { Undo(producer); continue; }
        Require(stack_.size() < stack_.capacity()); stack_.push_back({producer});
      }
    }
  }
  void Undo(OpId producer) { overlay_[producer.value].reset(); added_[producer.value] = 0; }
  void Reach(OpId root) {
    stack_.clear(); stack_.push_back({root});
    while (!stack_.empty()) {
      budget_.Step(); const auto node = stack_.back().op; stack_.pop_back();
      reached_[node.value] = 1;
      const auto& op = graph_.ops[node.value];
      if(MemberUpdateAt(plan_,node))continue;
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        budget_.Step(); const auto link = Edge(node, UseId{op.first_use.value + ordinal}, false);
        if (!link.producer) continue;
        Require(stack_.size() < stack_.capacity()); stack_.push_back({*link.producer});
      }
    }
  }
  struct Link {
    std::optional<OpId> selection;
    std::optional<OpId> producer;
    bool added = false;
  };
  bool Committed(OpId branch) const {
    return plan_.operations[branch.value].kind == EvaluationKind::kLogicalValue &&
        (branch.value >= plan_.phase_children.size() || !plan_.phase_children[branch.value]);
  }
  bool StatementRoot(OpId id) const {
    const auto& op = graph_.ops[id.value];
    const auto& placement = plan_.operations[id.value];
    return op.life == ValueLife::kLive &&
        (placement.kind == EvaluationKind::kRoot || placement.kind == EvaluationKind::kPhiInput) &&
        (PhaseCorridors::Expression(op.operation) || PhaseCorridors::ConsumesOperands(op.operation));
  }
  OpEvaluation Placement(OpId op) const {
    if (overlay_[op.value]) return {EvaluationKind::kInline, overlay_[op.value], {}};
    return plan_.operations[op.value];
  }
  bool Owned(OpId producer) const {
    return (producer.value < plan_.phase_prefixes.size() && plan_.phase_prefixes[producer.value]) ||
        (producer.value < plan_.phase_references.size() && plan_.phase_references[producer.value]);
  }
  bool Transport(OpId producer) const {
    return producer.value < plan_.phase_transports.size() && plan_.phase_transports[producer.value];
  }
  // Coordinate-free fusion semantics shared by builder and verifier: the one
  // canonical use, operand role/snapshot rules, and the producer semantics of
  // CanFuse. Order and catch obligations are checked by Compose.
  bool Fusable(OpId producer, OpId consumer, UseId use) const {
    const auto& parent = graph_.ops[consumer.value];
    const auto& occurrence = graph_.uses[use.value];
    const auto shape = DescribeEvaluation(parent.operation);
    return shape && !shape->NeedsSnapshot(occurrence.role) && occurrence.role != OperandRole::kReceiver &&
        !PredicateSnapshot(graph_, parent, occurrence.operand) &&
        corridors_.FuseSemantics(producer, consumer, use).has_value();
  }
  const LiteralReferenceView* LiteralAtom(OpId id) const {
    return id.value < literal_atoms_.size() && literal_atoms_[id.value]
               ? &*literal_atoms_[id.value]
               : nullptr;
  }
  bool Eligible(OpId producer, OpId consumer, UseId use) const {
    const auto& placement = plan_.operations[producer.value];
    return placement.kind == EvaluationKind::kRoot && placement.temporary != TemporaryReason::kMultipleUses &&
        placement.temporary != TemporaryReason::kPredicateSnapshot && !excluded_[producer.value] &&
        !region_of_[producer.value] && !Owned(producer) && !Transport(producer) && Fusable(producer, consumer, use);
  }
  // One operand occurrence of a node: a committed selection, an owned
  // producer (actual inline edge, or in kBuild a newly eligible root), or
  // external (receiver, snapshot, constant).
  Link Edge(OpId consumer, UseId use, bool build) const {
    if (plan_.operations[consumer.value].kind == EvaluationKind::kGuardedCellRead) return {};
    const auto& occurrence = graph_.uses[use.value];
    if (IsMemberReceiver(graph_, plan_, occurrence)) return {};
    const auto operand = ResolveEvaluationOperand(plan_, occurrence.operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value) return {};
    if (value->value < plan_.logical_values.size() && plan_.logical_values[value->value]) {
      const auto branch = *plan_.logical_values[value->value];
      if (Committed(branch) && region_of_[branch.value] && plan_.operations[branch.value].consumer == use)
        return {branch};
    }
    const auto result = EvaluationProducer(graph_, plan_, use);
    if (!result) return {};
    const auto producer = *result;
    const auto placement = Placement(producer);
    if (placement.kind == EvaluationKind::kInline && placement.consumer == use) return {std::nullopt, producer};
    if (const auto* atom = LiteralAtom(producer);
        atom && atom->call == consumer && atom->argument == use &&
        placement.kind == EvaluationKind::kRoot && build)
      return {std::nullopt, producer, true};
    if (build && Eligible(producer, consumer, use)) return {std::nullopt, producer, true};
    return {};
  }
  // Nearest stop at or before a cursor. Event status is static (composition
  // only turns roots into inline members), so per-block event prefixes and
  // memoized block-entry stops make every query O(1) amortized.
  Stop Next(Cursor at) {
    budget_.Step();
    if (at.end) {
      const auto& work = graph_.blocks[at.block.value];
      if (const auto index = latest_[work.first_op.value + at.end - 1])
        return {Stop::Kind::kEvent, OpId{work.first_op.value + *index}, 0, {at.block, *index}};
    }
    return Entry(at.block);
  }
  Stop Entry(WorkBlockId start) {
    // Resolve the chain of pass-through blocks once; all share one stop.
    chain_.clear();
    auto block = start;
    Stop stop;
    for (;;) {
      budget_.Step();
      if (entry_state_[block.value] == 2) { stop = entry_stop_[block.value]; break; }
      if (entry_state_[block.value] == 1) { stop = {}; break; }  // A pass-through cycle blocks.
      entry_state_[block.value] = 1; chain_.push_back(block);
      if (graph_.phases[block.value].phase == ExceptionPhase::kCatch) { stop = {}; break; }
      if (join_of_[block.value]) {
        stop = shared_join_[block.value] ? Stop{} : Stop{Stop::Kind::kJoin, {}, *join_of_[block.value], {block, 0}};
        break;
      }
      // An ordinary step: one normal entry, copy-free, whose source always
      // continues here. Exceptional edges are covered by the catch checks.
      if (incoming_[block.value] != 1 || !predecessor_[block.value]) { stop = {}; break; }
      const auto edge = *predecessor_[block.value];
      const auto source = graph_.edges[edge.value].source;
      if (copied_[edge.value] || outgoing_[source.value] != 1) { stop = {}; break; }
      const auto& work = graph_.blocks[source.value];
      if (work.op_count) {
        if (const auto index = latest_[work.first_op.value + work.op_count - 1]) {
          stop = {Stop::Kind::kEvent, OpId{work.first_op.value + *index}, 0, {source, *index}};
          break;
        }
      }
      block = source;
    }
    for (const auto member : chain_) { budget_.Step(); entry_state_[member.value] = 2; entry_stop_[member.value] = stop; }
    return stop;
  }
  // Every throw point between a committed selection's header and its join:
  // nullopt if they disagree or carry catch copies; otherwise one
  // representative (or none). Every normal path must reach the join.
  std::optional<std::optional<OpId>> RegionThrower(std::size_t index) {
    if (region_state_[index] == 1) return region_throw_[index];
    if (region_state_[index] == 2) return std::nullopt;
    region_state_[index] = 2;
    const auto& region = regions_[index];
    std::optional<OpId> thrower;
    ++stamp_; queue_.clear();
    const auto& header = graph_.blocks[region.header.value];
    for (std::uint32_t e = 0; e < header.edge_count; ++e) {
      budget_.Step(); const auto& edge = graph_.edges[header.first_edge.value + e];
      if (edge.kind == FlowEdgeKind::kException) continue;
      if (queue_.size() == queue_.capacity()) return std::nullopt;
      queue_.push_back(edge.target);
    }
    for (std::size_t at = 0; at < queue_.size(); ++at) {
      budget_.Step(); const auto block = queue_[at];
      if (block == region.join || visited_[block.value] == stamp_) continue;
      visited_[block.value] = stamp_;
      const auto& work = graph_.blocks[block.value];
      for (std::uint32_t i = 0; i < work.op_count; ++i) {
        budget_.Step(); const OpId id{work.first_op.value + i};
        const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive || !EvaluationMayThrow(graph_, plan_, id)) continue;
        if (corridors_.HasCatchCopies(id) || (thrower && !corridors_.SameCatch(id, *thrower))) return std::nullopt;
        if (!thrower) thrower = id;
      }
      bool exits = false;
      for (std::uint32_t e = 0; e < work.edge_count; ++e) {
        budget_.Step(); const auto& edge = graph_.edges[work.first_edge.value + e];
        if (edge.kind == FlowEdgeKind::kException) continue;
        if (queue_.size() == queue_.capacity()) return std::nullopt;
        exits = true; queue_.push_back(edge.target);
      }
      if (!exits) return std::nullopt;
    }
    region_state_[index] = 1; region_throw_[index] = thrower;
    return thrower;
  }

  const ValueProgram& graph_;
  const EvaluationPlan& plan_;
  TreeCheckBudget& budget_;
  PhaseBudget phase_budget_;
  PhaseCorridors corridors_;
  std::uint64_t accounted_work_ = 0;
  bool checked_mode_;
  std::vector<std::uint8_t> checked_tree_, repaired_;
  std::vector<std::optional<OpId>> seed_, checked_source_, module_ends_;
  std::vector<std::optional<GuardedCallFailure>> failures_;
  std::vector<SelectionRegion> regions_;
  std::vector<std::optional<LiteralReferenceView>> literal_atoms_;
  std::vector<std::optional<OpId>> literal_members_;
  std::vector<std::optional<std::size_t>> region_of_, join_of_;
  std::vector<std::uint32_t> incoming_, outgoing_, visited_;
  std::vector<std::optional<EdgeId>> predecessor_;
  std::vector<std::uint8_t> copied_, excluded_, shared_join_, added_, valid_, events_, selection_, fresh_, region_state_,
      reached_, entry_state_;
  std::vector<std::optional<std::uint32_t>> latest_;
  std::vector<Stop> entry_stop_;
  std::vector<WorkBlockId> chain_;
  std::vector<std::optional<OpId>> owner_, last_, region_throw_;
  std::vector<std::size_t> owned_count_;
  std::vector<std::optional<UseId>> overlay_;
  std::vector<Cursor> begin_;
  std::vector<Frame> stack_;
  std::vector<OpId> nodes_;
  std::vector<WorkBlockId> queue_;
  std::uint32_t stamp_ = 0;
};
}  // namespace
void ComposePhaseExpressions(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  if (plan.flow != EvaluationFlow::kPhased || (plan.phase_selections.empty() && plan.member_updates.updates.empty())) return;
  std::vector<std::pair<OpId, UseId>> edges;
  budget.Admit(graph.ops.size(), sizeof(std::pair<OpId, UseId>)); edges.reserve(graph.ops.size());
  budget.Admit(edges.capacity() - graph.ops.size(), sizeof(std::pair<OpId, UseId>));
  std::vector<OpId> roots;
  {
    Composition proof(graph, plan, budget);
    roots = proof.Build(edges);
    proof.Account();
  }
  if (roots.empty()) return;
  std::vector<OpEvaluation> original;
  budget.Admit(edges.size(), sizeof(OpEvaluation)); original.reserve(edges.size());
  budget.Admit(original.capacity() - edges.size(), sizeof(OpEvaluation));
  for (const auto& [producer, use] : edges) {
    budget.Step(); original.push_back(plan.operations[producer.value]);
    plan.operations[producer.value] = {EvaluationKind::kInline, use, {}};
  }
  auto previous = std::move(plan.phase_compositions);
  if(previous.empty())plan.phase_compositions=std::move(roots);
  else {
    Require(roots.size()<=std::numeric_limits<std::size_t>::max()-previous.size());
    const auto capacity=previous.size()+roots.size();
    budget.Admit(capacity,sizeof(OpId));
    plan.phase_compositions.reserve(capacity);
    budget.Admit(plan.phase_compositions.capacity()-capacity,sizeof(OpId));
    std::set_union(previous.begin(),previous.end(),roots.begin(),roots.end(),std::back_inserter(plan.phase_compositions));
  }
  // Conservative gate: the selection proof must accept exactly the same
  // regions, owners, prefixes and references; only execution may change.
  if (RefreshPhaseSelectionExecutions(graph, plan, budget)) return;
  for (std::size_t i = 0; i < edges.size(); ++i) { budget.Step(); plan.operations[edges[i].first.value] = original[i]; }
  if(previous.empty())plan.phase_compositions.clear();
  else plan.phase_compositions=std::move(previous);
}
void ComposeCheckedCalls(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  if (plan.flow != EvaluationFlow::kPhased || plan.guarded_reads.empty()) return;
  bool needed = false;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const OpId call{static_cast<std::uint32_t>(i)};
    if (const auto property = MemberProperty(graph, plan, call)) needed |= !MemberReferenceInline(graph, plan, call, *property);
  }
  if (!needed) return; // Stable plans never acquire new names or placements.
  std::vector<std::pair<OpId, UseId>> edges;
  budget.Admit(graph.ops.size(), sizeof(std::pair<OpId, UseId>)); edges.reserve(graph.ops.size());
  budget.Admit(edges.capacity() - graph.ops.size(), sizeof(std::pair<OpId, UseId>));
  Composition proof(graph, plan, budget, true);
  auto roots = proof.Build(edges); proof.Failures(plan.checked_failures); proof.Account();
  if (roots.empty()) return;
  std::vector<OpEvaluation> original;
  budget.Admit(edges.size(), sizeof(OpEvaluation)); original.reserve(edges.size());
  budget.Admit(original.capacity() - edges.size(), sizeof(OpEvaluation));
  for (const auto& [producer, use] : edges) {
    budget.Step(); original.push_back(plan.operations[producer.value]);
    plan.operations[producer.value] = {EvaluationKind::kInline, use, {}};
  }
  plan.checked_compositions = std::move(roots);
  if (RefreshPhaseSelectionExecutions(graph, plan, budget)) return;
  for (std::size_t i = 0; i < edges.size(); ++i) { budget.Step(); plan.operations[edges[i].first.value] = original[i]; }
  plan.checked_compositions.clear();
}
std::vector<std::uint8_t> VerifyPhaseCompositions(const ValueProgram& graph, const EvaluationPlan& plan,
    TreeCheckBudget& budget) {
  std::vector<std::uint8_t> mask;
  if (plan.phase_compositions.empty() && plan.checked_compositions.empty()) return mask;
  Require(plan.flow == EvaluationFlow::kPhased);
  Require(plan.phase_compositions.empty() || !plan.phase_selections.empty() || !plan.member_updates.updates.empty());
  budget.Array(mask, graph.ops.size());
  Composition proof(graph, plan, budget);
  for (std::size_t i = 0; i < plan.phase_compositions.size(); ++i) {
    budget.Step(); const auto root = plan.phase_compositions[i];
    Require(!i || plan.phase_compositions[i - 1] < root);
    for (const auto node : proof.Verify(root)) { budget.Step(); Require(!mask[node.value]); mask[node.value] = 1; }
  }
  proof.MaskLiteralMembers(mask);
  proof.Account();
  if (!plan.checked_compositions.empty()) {
    Composition checked(graph, plan, budget, true);
    for (std::size_t i = 0; i < plan.checked_compositions.size(); ++i) {
      budget.Step(); const auto root = plan.checked_compositions[i];
      Require(!i || plan.checked_compositions[i - 1] < root);
      for (const auto node : checked.Verify(root)) {
        budget.Step(); Require(!mask[node.value]); mask[node.value] = 1;
        if (GuardedInput(plan, node)) {
          const auto guard = *GuardedPeer(plan, node);
          Require(!mask[guard.value]); mask[guard.value] = 1;
        }
      }
    }
    checked.Account();
  }
  for (const auto& pair : plan.guarded_reads) {
    budget.Step();
    if (plan.operations[pair.read.value].kind == EvaluationKind::kInline) Require(mask[pair.read.value] && mask[pair.guard.value]);
  }
  return mask;
}
}  // namespace abcd::source
