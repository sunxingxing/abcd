#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_evaluation.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool condition) { if (!condition) throw EvaluationError::kOwnership; }
struct Interval { std::size_t count = 0, end = 0; };
bool Inputs(EvaluationKind kind) {
  return kind == EvaluationKind::kRoot || kind == EvaluationKind::kInline || kind == EvaluationKind::kPredicateSnapshot ||
      kind == EvaluationKind::kLogicalLeft || kind == EvaluationKind::kLogicalValue;
}
std::vector<SelectionRegion> Regions(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  if (plan.flow != EvaluationFlow::kNormal || !plan.phis) return {};
  bool predicate = false;
  for (const auto& op : graph.ops) {
    budget.Step(); const auto* branch = std::get_if<ValueBranch>(&op.operation);
    if (op.life == ValueLife::kLive && branch) { predicate = true; break; }
  }
  if (!predicate) return {};
  auto built = BuildControlPlan(graph, budget.Remaining());
  const auto* control = std::get_if<ControlPlan>(&built);
  if (!control) {
    const auto error = std::get<ControlError>(built);
    if (error == ControlError::kResourceLimit) throw EvaluationError::kResourceLimit;
    if (error == ControlError::kWorkLimit) throw EvaluationError::kWorkLimit;
    return {};
  }
  budget.Admit(control->allocated_bytes, 1); budget.Step(control->work);
  return FindSelectionExpressions(graph, *control, plan, budget);
}
std::size_t Events(const ValueOp& op, const OpEvaluation& placement, OpId id, OpId branch) {
  if (id == branch) return 1; // One conditional region; RHS is independently owned/checked.
  if (placement.kind == EvaluationKind::kCellGuard || placement.kind == EvaluationKind::kModuleGuard) return 0;
  if (std::holds_alternative<ValueModuleRead>(op.operation)) return 2;
  return OrderedEvaluation(op) || IsEnvironmentState(op.operation);
}
std::optional<UseId> Consumer(const ValueProgram& graph, const SelectionRegion& region, TreeCheckBudget& budget) {
  std::optional<UseId> found;
  for (auto use = graph.values[region.merged.value].first_use; use; use = graph.uses[use->value].next) {
    budget.Step(); const auto& occurrence = graph.uses[use->value];
    if (occurrence.life != ValueLife::kLive) continue;
    const auto* owner = std::get_if<OpId>(&occurrence.owner);
    if (found || !owner || graph.ops[owner->value].block != region.join) return {};
    const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
    if (!shape || shape->NeedsSnapshot(occurrence.role) || occurrence.role == OperandRole::kReceiver ||
        PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand)) return {};
    found = use;
  }
  return found;
}
// A selected initializer may own its previously pinned local RHS. Restore that
// exact snapshot for the original local proof; the domain separately proves its
// movement and unique consumption into the property definition.
bool FieldValue(const ValueProgram& graph, const EvaluationPlan& plan, OpId id) {
  const auto& placement = plan.operations[id.value];
  if (placement.kind != EvaluationKind::kInline || !placement.consumer ||
      placement.consumer->value >= graph.uses.size()) return false;
  const auto& use = graph.uses[placement.consumer->value];
  const auto* owner = std::get_if<OpId>(&use.owner);
  return owner && owner->value < graph.ops.size() && use.role == OperandRole::kStoredValue &&
      graph.ops[owner->value].block == graph.ops[id.value].block &&
      plan.operations[owner->value].kind == EvaluationKind::kInitializer &&
      IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, *owner);
}
struct SelectionBlock {
  bool header = false;
  std::optional<OpId> prefix_end;
};
bool Available(const ValueProgram& graph, const SelectionRegion& region, const std::vector<SelectionBlock>& used) {
  const auto& header = used[region.header.value]; const auto& join_block = used[region.join.value];
  if (!region.complete_phi || region.parent || header.header || join_block.header || join_block.prefix_end) return false;
  // A preceding selection ends at its join consumer, not at the end of that
  // block. The suffix may be another header; Domain still checks all intervening
  // events and exclusive dependencies. Each block is scanned at most twice.
  if (header.prefix_end && graph.ops[header.prefix_end->value].order >= graph.ops[region.boolean.value].order) return false;
  const auto join = graph.blocks[region.join.value].first_op.value;
  if (region.last_dependency) return region.last_dependency->value < join;
  return region.branch.value < join && graph.blocks[region.evaluate.value].first_op.value < join;
}
struct SelectionIndex {
  std::vector<std::optional<std::size_t>> headers, objects;
  std::vector<std::optional<UseId>> consumers;
  std::vector<std::optional<OpId>> fields, expressions;
  std::vector<std::optional<std::size_t>> field_regions;
  std::vector<std::optional<OpId>> first_selections;
  std::vector<std::uint8_t> ambiguous_fields;
};
SelectionIndex IndexSelections(const ValueProgram& graph, const EvaluationPlan& plan,
    std::span<const SelectionRegion> regions, TreeCheckBudget& budget) {
  SelectionIndex index;
  budget.Array(index.headers, graph.blocks.size()); budget.Array(index.objects, graph.ops.size());
  budget.Array(index.consumers, graph.ops.size()); budget.Array(index.fields, graph.ops.size());
  budget.Array(index.expressions, graph.ops.size());
  budget.Array(index.field_regions, graph.ops.size()); budget.Array(index.first_selections, graph.ops.size());
  budget.Array(index.ambiguous_fields, graph.ops.size());
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); const auto& region = regions[i];
    if (region.parent || !region.complete_phi) continue;
    Require(!index.headers[region.header.value]); index.headers[region.header.value] = i;
    index.consumers[region.branch.value] = Consumer(graph, region, budget);
  }
  if (plan.literals) for (std::size_t i = 0; i < plan.literals->objects.size(); ++i) {
    budget.Step(); const auto& object = plan.literals->objects[i];
    if (object.form != LiteralForm::kStatements) continue;
    index.objects[object.allocation.value] = i;
    for (std::uint32_t j = 0; j < object.count; ++j) {
      budget.Step(); index.fields[plan.literals->definitions[object.first + j].value] = object.allocation;
    }
  }
  // Reverse value order propagates field ownership through existing local
  // inline trees and the sole pinned RHS root. Each operand is visited once.
  for (std::size_t i = graph.ops.size(); i-- > 0;) {
    budget.Step(); const auto& op = graph.ops[i];
    if (index.fields[i]) index.expressions[i] = OpId{static_cast<std::uint32_t>(i)};
    if (!index.expressions[i]) continue;
    for (std::uint32_t j = 0; j < op.use_count; ++j) {
      budget.Step(); const UseId use{op.first_use.value + j};
      if (index.fields[i] && graph.uses[use.value].role != OperandRole::kStoredValue) continue;
      const auto* value = std::get_if<ValueId>(&graph.uses[use.value].operand);
      const auto* result = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
      if (!result || result->op.value >= i || graph.ops[result->op.value].block != op.block) continue;
      const auto placement = BeforeLogicalFusion(graph, plan, result->op.value);
      const bool root = index.fields[i] && placement.kind == EvaluationKind::kRoot &&
          placement.temporary == TemporaryReason::kEffectSnapshot;
      const bool owned = placement.kind == EvaluationKind::kInline && placement.consumer == use;
      if (root || owned) {
        Require(!index.expressions[result->op.value]);
        index.expressions[result->op.value] = index.expressions[i];
      }
    }
  }
  // Only the first selected field can start an object's route. Index this once
  // so ordinary prefix/suffix fields do not cause a whole-object retry at each
  // later selection. Multiple selections within one RHS need another domain.
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); const auto& region = regions[i];
    const auto use = index.consumers[region.branch.value];
    if (!use) continue;
    const auto owner = std::get<OpId>(graph.uses[use->value].owner);
    const auto field = index.expressions[owner.value];
    if (!field || !index.fields[field->value]) continue;
    if (index.field_regions[field->value]) index.ambiguous_fields[field->value] = 1;
    else index.field_regions[field->value] = i;
    auto& first = index.first_selections[index.fields[field->value]->value];
    if (!first || region.branch < *first) first = region.branch;
  }
  return index;
}
struct SelectedObject { OpId allocation; std::span<const OpId> definitions; UseId consumer; };
// Follow adjacent complete selections and intervening ordinary definitions in
// original field order. Only the first-selection candidate visits the object's
// fields/use chain; no per-field whole-object or whole-CFG search is needed.
std::optional<SelectedObject> ObjectRoute(const ValueProgram& graph, const EvaluationPlan& plan,
    std::span<const SelectionRegion> regions, const SelectionIndex& index,
    const std::vector<SelectionBlock>& used, std::vector<const SelectionRegion*>& route, TreeCheckBudget& budget) {
  const auto first_use = index.consumers[route.front()->branch.value];
  if (!first_use || !plan.literals) return {};
  const auto first_owner = std::get<OpId>(graph.uses[first_use->value].owner);
  if (!index.expressions[first_owner.value]) return {};
  const auto first = *index.expressions[first_owner.value];
  if (!index.fields[first.value]) return {};
  const auto allocation = *index.fields[first.value];
  const auto& object = plan.literals->objects[*index.objects[allocation.value]];
  const auto definitions = std::span(plan.literals->definitions).subspan(object.first, object.count);
  if (definitions.empty() || index.first_selections[allocation.value] != route.front()->branch ||
      graph.ops[allocation.value].block != route.front()->header) return {};
  const auto* buffer = std::get_if<ValueObjectAllocation>(&graph.ops[allocation.value].operation);
  if (!buffer || !buffer->literal) return {};
  auto block = route.front()->header;
  bool selected = false;
  for (const auto definition : definitions) {
    budget.Step();
    if (index.ambiguous_fields[definition.value]) return {};
    if (const auto next = index.field_regions[definition.value]) {
      const auto& region = regions[*next];
      if (region.header != block || !Available(graph, region, used)) return {};
      if (selected) {
        if (region.branch <= route.back()->branch) return {};
        route.push_back(&region);
      } else if (&region != route.front()) return {};
      selected = true; block = region.join;
    }
    const auto& field = graph.ops[definition.value];
    if (field.use_count != 2 || !std::holds_alternative<ValueOwnWrite>(field.operation) || field.block != block) return {};
  }
  if (!selected) return {};
  const auto& final = graph.ops[definitions.back().value];
  std::optional<UseId> external;
  for (auto at = graph.values[graph.ops[allocation.value].result->value].first_use; at; at = graph.uses[at->value].next) {
    budget.Step(); const auto& use = graph.uses[at->value];
    if (use.life != ValueLife::kLive) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (owner && index.fields[owner->value] == allocation && *at == graph.ops[owner->value].first_use) continue;
    if (external || !owner || graph.ops[owner->value].block != route.back()->join ||
        graph.ops[owner->value].order <= final.order) return {};
    const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
    if (!shape || shape->NeedsSnapshot(use.role) || use.role == OperandRole::kReceiver ||
        PredicateSnapshot(graph, graph.ops[owner->value], use.operand)) return {};
    external = *at;
  }
  if (!external) return {};
  return SelectedObject{allocation, definitions, *external};
}
struct DomainIndex {
  std::vector<std::uint8_t> blocks, owners, needed;
  std::vector<OpId> needed_ops;
  std::vector<std::optional<OpId>> values;
  std::vector<std::optional<UseId>> branches;
  DomainIndex(const ValueProgram& graph, TreeCheckBudget& budget) {
    budget.Array(blocks, graph.blocks.size()); budget.Array(owners, graph.ops.size());
    budget.Array(values, graph.values.size()); budget.Array(branches, graph.ops.size());
    budget.Array(needed, graph.ops.size()); budget.Array(needed_ops, graph.ops.size()); needed_ops.clear();
  }
};
// Contract a forward chain to its header blocks plus final join. Each selection
// is one conditional event whose arms remain independently checked. A private
// object interval grows at each own definition and is consumed exactly once.
// Shared header/join blocks are visited once, so fields cannot duplicate effects.
class Domain {
 public:
  Domain(const ValueProgram& graph, const EvaluationPlan& plan, const SelectionIndex& selections,
      std::span<const SelectionRegion* const> route, std::vector<Interval>& intervals,
      DomainIndex& index, TreeCheckBudget& budget, std::optional<SelectedObject> object = {})
      : graph_(graph), plan_(plan), route_(route), intervals_(intervals), index_(index), budget_(budget), object_(object),
        last_owner_(std::get<OpId>(graph.uses[(object ? object->consumer :
            *selections.consumers[route.back()->branch.value]).value].owner)) {
    Require(graph.ops[last_owner_.value].block == route.back()->join);
    // Prepay the nonthrowing cleanup as well as index construction.
    budget_.Step(2 * (route.size() + 1 + (object_ ? object_->definitions.size() : 0)));
    for (const auto* region : route) {
      index_.blocks[region->header.value] = 1;
      index_.values[region->merged.value] = region->branch;
      index_.branches[region->branch.value] = selections.consumers[region->branch.value];
      const auto owner = std::get<OpId>(graph.uses[index_.branches[region->branch.value]->value].owner);
      index_.owners[owner.value] = 1;
    }
    index_.blocks[route.back()->join.value] = 1;
    if (object_) {
      index_.owners[std::get<OpId>(graph.uses[object_->consumer.value].owner).value] = 1;
      for (const auto field : object_->definitions) index_.owners[field.value] = 1;
    }
    Dependencies();
  }
  ~Domain() {
    for (const auto op : index_.needed_ops) index_.needed[op.value] = 0;
    index_.needed_ops.clear();
    for (const auto* region : route_) {
      index_.blocks[region->header.value] = 0; index_.values[region->merged.value].reset();
      const auto use = *index_.branches[region->branch.value]; index_.branches[region->branch.value].reset();
      index_.owners[std::get<OpId>(graph_.uses[use.value].owner).value] = 0;
    }
    index_.blocks[route_.back()->join.value] = 0;
    if (object_) {
      index_.owners[std::get<OpId>(graph_.uses[object_->consumer.value].owner).value] = 0;
      for (const auto field : object_->definitions) index_.owners[field.value] = 0;
    }
  }
  bool Build(std::vector<std::pair<OpId, UseId>>& selected) {
    std::size_t serial = 0, definition = 0; bool object_consumed = false;
    for (std::size_t b = 0; b <= route_.size(); ++b) {
      const auto& range = Block(b);
      for (std::uint32_t j = 0; j < Count(b); ++j) {
        budget_.Step(); const OpId id{range.first_op.value + j}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        const bool field = object_ && definition < object_->definitions.size() && id == object_->definitions[definition];
        const auto count = Event(id); intervals_[id.value] = {count, serial + count};
        if (!index_.needed[id.value]) { serial += count; continue; }
        auto expected = serial;
        for (auto ordinal = op.use_count; (Inputs(plan_.operations[id.value].kind) || field) && ordinal != 0; --ordinal) {
          budget_.Step(); const UseId use{op.first_use.value + ordinal - 1}; const auto& occurrence = graph_.uses[use.value];
          if (IsMemberReceiver(graph_, plan_, occurrence)) continue;
          const auto producer = Producer(occurrence); if (!producer || !Inside(*producer)) continue;
          const auto& placement = plan_.operations[producer->value];
          const bool logical = index_.branches[producer->value] == use;
          const bool base = field && *producer == object_->allocation;
          const bool literal = object_ && use == object_->consumer && *producer == object_->allocation;
          const bool owned = (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kLogicalLeft || placement.kind == EvaluationKind::kPredicateSnapshot) && placement.consumer == use;
          const bool candidate = Candidate(id, occurrence, *producer, field);
          if (!owned && !logical && !candidate && !base && !literal) continue;
          const auto interval = intervals_[producer->value];
          if (interval.count && interval.end != expected) {
            if (owned || logical || base || literal) return false;
            continue;
          }
          Require(interval.count <= expected); expected -= interval.count; intervals_[id.value].count += interval.count;
          if (candidate && !base && !literal) selected.emplace_back(*producer, use);
          if (literal) object_consumed = true;
        }
        if (field) { intervals_[object_->allocation.value] = intervals_[id.value]; ++definition; }
        serial += count;
      }
    }
    return !object_ || (object_consumed && definition == object_->definitions.size());
  }
  void Verify(std::vector<std::uint8_t>& consumed) {
    std::size_t serial = 0, definition = 0;
    for (std::size_t b = 0; b <= route_.size(); ++b) {
      const auto& range = Block(b);
      for (std::uint32_t j = 0; j < Count(b); ++j) {
        budget_.Step(); const OpId id{range.first_op.value + j}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        const bool field = object_ && definition < object_->definitions.size() && id == object_->definitions[definition];
        const auto count = Event(id); intervals_[id.value] = {count, serial + count};
        if (!index_.needed[id.value]) { serial += count; continue; }
        std::optional<Interval> children;
        for (std::uint32_t ordinal = 0; (Inputs(plan_.operations[id.value].kind) || field) && ordinal < op.use_count; ++ordinal) {
          budget_.Step(); const UseId use{op.first_use.value + ordinal}; const auto& occurrence = graph_.uses[use.value];
          if (IsMemberReceiver(graph_, plan_, occurrence)) continue;
          const auto producer = Producer(occurrence); if (!producer || !Inside(*producer)) continue;
          const auto& placement = plan_.operations[producer->value];
          if (placement.kind == EvaluationKind::kPredicateSnapshot && placement.consumer != use) continue;
          if (placement.kind != EvaluationKind::kInline && placement.kind != EvaluationKind::kLogicalLeft &&
              placement.kind != EvaluationKind::kLogicalValue && placement.kind != EvaluationKind::kPredicateSnapshot) continue;
          const bool base = field && *producer == object_->allocation;
          Require(base || placement.consumer == use);
          if (!base && (placement.kind == EvaluationKind::kLogicalValue || CrossLogicalInline(graph_, plan_, *producer) || FieldValue(graph_, plan_, *producer))) {
            Require(index_.owners[id.value] && !consumed[producer->value]);
            const auto shape = DescribeEvaluation(op.operation);
            Require(shape && !shape->NeedsSnapshot(occurrence.role) && occurrence.role != OperandRole::kReceiver &&
                !PredicateSnapshot(graph_, op, occurrence.operand));
            consumed[producer->value] = 1;
          }
          const auto interval = intervals_[producer->value]; Require(interval.count <= interval.end);
          if (!interval.count) continue;
          if (children) {
            Require(children->end == interval.end - interval.count);
            children->count += interval.count; children->end = interval.end;
          } else children = interval;
        }
        if (children) { Require(children->end == serial); intervals_[id.value].count += children->count; }
        if (field) {
          Require(!consumed[id.value]); consumed[id.value] = 1;
          intervals_[object_->allocation.value] = intervals_[id.value]; ++definition;
        }
        serial += count;
      }
    }
    Require(!object_ || definition == object_->definitions.size());
  }
 private:
  bool Candidate(OpId id, const OperandUse& occurrence, OpId producer, bool field) const {
    const auto& op = graph_.ops[id.value]; const auto& placement = plan_.operations[producer.value];
    const auto shape = DescribeEvaluation(op.operation);
    const bool field_value = field && occurrence.role == OperandRole::kStoredValue &&
        graph_.ops[producer.value].block == op.block && placement.temporary == TemporaryReason::kEffectSnapshot;
    return index_.owners[id.value] && placement.kind == EvaluationKind::kRoot &&
        (placement.temporary == TemporaryReason::kCrossRegionValue || field_value) && shape &&
        !shape->NeedsSnapshot(occurrence.role) && occurrence.role != OperandRole::kReceiver &&
        !PredicateSnapshot(graph_, op, occurrence.operand);
  }
  void Needed(OpId id) {
    if (index_.needed[id.value]) return;
    budget_.Step(2); // Mark now and prepay nonthrowing cleanup.
    index_.needed[id.value] = 1; index_.needed_ops.push_back(id);
  }
  void Dependencies() {
    Needed(last_owner_);
    for (const auto* region : route_) { budget_.Step(); Needed(region->branch); }
    if (object_) {
      Needed(object_->allocation);
      for (const auto field : object_->definitions) { budget_.Step(); Needed(field); }
    }
    auto definition = object_ ? object_->definitions.size() : 0;
    // Reverse the contracted region order once. Only the final consumers and
    // the trees they own need the additional composition proof; all intervening
    // effects still advance the forward event clock in Build and Verify.
    for (auto b = route_.size() + 1; b != 0; --b) {
      const auto& range = Block(b - 1);
      for (auto j = Count(b - 1); j != 0; --j) {
        budget_.Step(); const OpId id{range.first_op.value + j - 1}; const auto& op = graph_.ops[id.value];
        const bool field = definition && id == object_->definitions[definition - 1];
        if (field) --definition;
        if (!index_.needed[id.value] || op.life != ValueLife::kLive) continue;
        for (std::uint32_t k = 0; (Inputs(plan_.operations[id.value].kind) || field) && k < op.use_count; ++k) {
          budget_.Step(); const UseId use{op.first_use.value + k}; const auto& occurrence = graph_.uses[use.value];
          if (IsMemberReceiver(graph_, plan_, occurrence)) continue;
          const auto producer = Producer(occurrence); if (!producer || !Inside(*producer)) continue;
          const auto& placement = plan_.operations[producer->value];
          const bool owned = (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kLogicalLeft ||
              placement.kind == EvaluationKind::kLogicalValue || placement.kind == EvaluationKind::kPredicateSnapshot) && placement.consumer == use;
          const bool object = object_ && *producer == object_->allocation && (field || use == object_->consumer);
          if (owned || object || index_.branches[producer->value] == use || Candidate(id, occurrence, *producer, field)) Needed(*producer);
        }
      }
    }
  }
  const WorkBlock& Block(std::size_t index) const {
    const auto id = index < route_.size() ? route_[index]->header : route_.back()->join;
    return graph_.blocks[id.value];
  }
  std::uint32_t Count(std::size_t block) const {
    // The transformation ends at its final consumer. Later join operations keep
    // their separately verified local schedule, including other literal groups.
    return block < route_.size() ? Block(block).op_count : graph_.ops[last_owner_.value].order + 1;
  }
  std::size_t Event(OpId id) const {
    if (index_.branches[id.value]) return 1;
    // A checked local literal is an atomic expression in this outer domain.
    // Its owned children/definitions have already been ordered by LiteralPlan;
    // counting them again would split that expression. Staged outer fields
    // remain distinct events, including after conversion to kInitializer.
    const auto kind = plan_.operations[id.value].kind;
    if (kind == EvaluationKind::kNestedLiteral ||
        (kind == EvaluationKind::kInitializer && !IsStagedLiteral(plan_.literals ? &*plan_.literals : nullptr, id))) return 0;
    return Events(graph_.ops[id.value], plan_.operations[id.value], id, route_.front()->branch);
  }
  std::optional<OpId> Producer(const OperandUse& use) const {
    const auto* value = std::get_if<ValueId>(&use.operand); if (!value) return {};
    if (index_.values[value->value]) return index_.values[value->value];
    const auto* result = std::get_if<OpResult>(&graph_.values[value->value].origin);
    return result ? std::optional{result->op} : std::nullopt;
  }
  bool Inside(OpId id) const { return index_.blocks[graph_.ops[id.value].block.value]; }
  const ValueProgram& graph_; const EvaluationPlan& plan_;
  std::span<const SelectionRegion* const> route_; std::vector<Interval>& intervals_;
  DomainIndex& index_; TreeCheckBudget& budget_; std::optional<SelectedObject> object_; OpId last_owner_;
};
}  // namespace
bool CrossLogicalInline(const ValueProgram& graph, const EvaluationPlan& plan, OpId producer) {
  const auto& placement = plan.operations[producer.value];
  if (plan.flow != EvaluationFlow::kNormal || placement.kind != EvaluationKind::kInline || !placement.consumer ||
      placement.consumer->value >= graph.uses.size()) return false;
  const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
  return owner && owner->value < graph.ops.size() && graph.ops[owner->value].block != graph.ops[producer.value].block;
}
OpEvaluation BeforeLogicalFusion(const ValueProgram& graph, const EvaluationPlan& plan, std::size_t operation) {
  const auto placement = plan.operations[operation];
  if (placement.kind == EvaluationKind::kPredicateSnapshot) return {EvaluationKind::kRoot, {}, TemporaryReason::kMultipleUses};
  if (!plan.logical_objects.empty() && plan.logical_objects[operation])
    return {EvaluationKind::kRoot, {}, TemporaryReason::kMultipleUses};
  if (placement.kind == EvaluationKind::kInitializer &&
      IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, OpId{static_cast<std::uint32_t>(operation)}))
    return {EvaluationKind::kRoot, {}, {}};
  if (FieldValue(graph, plan, OpId{static_cast<std::uint32_t>(operation)}))
    return {EvaluationKind::kRoot, {}, TemporaryReason::kEffectSnapshot};
  if (placement.kind == EvaluationKind::kLogicalValue) return {EvaluationKind::kRoot, {}, {}};
  if (CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(operation)}))
    return {EvaluationKind::kRoot, {}, TemporaryReason::kCrossRegionValue};
  return placement;
}
void BuildLogicalValues(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  BuildPredicateSnapshots(graph, plan, budget);
  const auto regions = Regions(graph, plan, budget); if (regions.empty()) return;
  const auto index = IndexSelections(graph, plan, regions, budget); DomainIndex domain_index(graph, budget);
  std::vector<Interval> intervals; std::vector<SelectionBlock> used;
  std::vector<std::pair<OpId, UseId>> selected; std::vector<const SelectionRegion*> route;
  budget.Array(intervals, graph.ops.size()); budget.Array(used, graph.blocks.size());
  budget.Admit(graph.ops.size(), sizeof(std::pair<OpId, UseId>)); selected.reserve(graph.ops.size());
  budget.Admit(selected.capacity() - graph.ops.size(), sizeof(std::pair<OpId, UseId>));
  budget.Admit(regions.size(), sizeof(const SelectionRegion*)); route.reserve(regions.size());
  budget.Admit(route.capacity() - regions.size(), sizeof(const SelectionRegion*));
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!index.consumers[i]) continue;
    const auto& region = regions[*index.headers[graph.ops[i].block.value]];
    if (!Available(graph, region, used)) continue;
    selected.clear(); route.clear(); route.push_back(&region);
    auto object = ObjectRoute(graph, plan, regions, index, used, route, budget);
    if (object && !Domain(graph, plan, index, route, intervals, domain_index, budget, object).Build(selected)) object.reset();
    if (!object) {
      selected.clear(); route.resize(1);
      if (!Domain(graph, plan, index, route, intervals, domain_index, budget).Build(selected)) continue;
    }
    if (object) {
      if (plan.logical_objects.empty()) budget.Array(plan.logical_objects, graph.ops.size());
      plan.logical_objects[object->allocation.value] = object->definitions.back();
      plan.operations[object->allocation.value] = {EvaluationKind::kInline, object->consumer, {}};
      for (const auto field : object->definitions) { budget.Step(); plan.operations[field.value] = {EvaluationKind::kInitializer, {}, {}}; }
    }
    if (plan.logical_values.empty()) budget.Array(plan.logical_values, graph.values.size());
    for (const auto* selected_region : route) {
      budget.Step(); plan.logical_values[selected_region->merged.value] = selected_region->branch;
      plan.operations[selected_region->branch.value] = {EvaluationKind::kLogicalValue, index.consumers[selected_region->branch.value], {}};
      used[selected_region->header.value].header = true;
      const auto consumer = selected_region == route.back() && object ? object->consumer :
          *index.consumers[selected_region->branch.value];
      used[selected_region->join.value].prefix_end = std::get<OpId>(graph.uses[consumer.value].owner);
    }
    for (const auto& [producer, use] : selected) { budget.Step(); plan.operations[producer.value] = {EvaluationKind::kInline, use, {}}; }
  }
  for (const auto& region : regions) {
    budget.Step(); if (!region.forwarded) continue;
    if (plan.logical_values.empty()) budget.Array(plan.logical_values, graph.values.size());
    plan.logical_values[region.merged.value] = region.branch;
    plan.operations[region.branch.value] = {EvaluationKind::kLogicalValue, region.forwarded, {}};
  }
}
void VerifyLogicalValues(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  VerifyPredicateSnapshots(graph, plan, budget);
  Require(plan.logical_objects.empty() || plan.logical_objects.size() == graph.ops.size());
  Require(plan.logical_values.empty() || (plan.flow == EvaluationFlow::kNormal && plan.logical_values.size() == graph.values.size()));
  if (plan.logical_values.empty()) {
    Require(plan.logical_objects.empty());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); Require(plan.operations[i].kind != EvaluationKind::kLogicalValue &&
          !(plan.operations[i].kind == EvaluationKind::kInitializer &&
            IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, OpId{static_cast<std::uint32_t>(i)})) &&
          !FieldValue(graph, plan, OpId{static_cast<std::uint32_t>(i)}) &&
          !CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)}));
    }
    return;
  }
  const auto regions = Regions(graph, plan, budget); const auto index = IndexSelections(graph, plan, regions, budget);
  DomainIndex domain_index(graph, budget);
  std::vector<Interval> intervals; std::vector<SelectionBlock> used;
  std::vector<std::uint8_t> consumed, values, objects;
  std::vector<const SelectionRegion*> route;
  budget.Array(intervals, graph.ops.size()); budget.Array(used, graph.blocks.size());
  budget.Array(consumed, graph.ops.size()); budget.Array(values, graph.values.size()); budget.Array(objects, graph.ops.size());
  budget.Admit(regions.size(), sizeof(const SelectionRegion*)); route.reserve(regions.size());
  budget.Admit(route.capacity() - regions.size(), sizeof(const SelectionRegion*));
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!index.consumers[i]) continue;
    const auto& region = regions[*index.headers[graph.ops[i].block.value]];
    if (!plan.logical_values[region.merged.value] || values[region.merged.value]) continue;
    Require(Available(graph, region, used) && index.consumers[region.branch.value]);
    route.clear(); route.push_back(&region);
    auto object = ObjectRoute(graph, plan, regions, index, used, route, budget);
    if (object && (plan.logical_objects.empty() || !plan.logical_objects[object->allocation.value])) object.reset();
    if (!object) route.resize(1);
    if (object) {
      objects[object->allocation.value] = 1;
      Require(plan.operations[object->allocation.value].kind == EvaluationKind::kInline &&
          plan.operations[object->allocation.value].consumer == object->consumer &&
          !plan.operations[object->allocation.value].temporary &&
          plan.logical_objects[object->allocation.value] == object->definitions.back());
      for (const auto field : object->definitions) {
        budget.Step(); Require(plan.operations[field.value].kind == EvaluationKind::kInitializer &&
            !plan.operations[field.value].consumer && !plan.operations[field.value].temporary);
      }
    }
    for (const auto* selected_region : route) {
      budget.Step(); const auto& placement = plan.operations[selected_region->branch.value];
      Require(plan.logical_values[selected_region->merged.value] == selected_region->branch &&
          placement.kind == EvaluationKind::kLogicalValue && placement.consumer == index.consumers[selected_region->branch.value] && !placement.temporary);
      values[selected_region->merged.value] = 1;
    }
    Domain(graph, plan, index, route, intervals, domain_index, budget, object).Verify(consumed);
    for (const auto* selected_region : route) {
      budget.Step(); used[selected_region->header.value].header = true;
      const auto consumer = selected_region == route.back() && object ? object->consumer :
          *index.consumers[selected_region->branch.value];
      used[selected_region->join.value].prefix_end = std::get<OpId>(graph.uses[consumer.value].owner);
    }
  }
  for (const auto& region : regions) {
    budget.Step(); if (!region.forwarded) continue;
    const auto& placement = plan.operations[region.branch.value];
    Require(region.parent && region.complete_phi && !values[region.merged.value] && !consumed[region.branch.value] &&
        plan.logical_values[region.merged.value] == region.branch && placement.kind == EvaluationKind::kLogicalValue &&
        placement.consumer == region.forwarded && !placement.temporary);
    values[region.merged.value] = 1; consumed[region.branch.value] = 1;
  }
  for (std::size_t i = 0; i < values.size(); ++i) { budget.Step(); Require(plan.logical_values[i].has_value() == (values[i] != 0)); }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    if (!plan.logical_objects.empty() && plan.logical_objects[i]) {
      Require(objects[i] && consumed[i] && plan.logical_objects[i]->value < graph.ops.size() && consumed[plan.logical_objects[i]->value]);
    }
    if (IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, OpId{static_cast<std::uint32_t>(i)}) &&
        plan.operations[i].kind == EvaluationKind::kInitializer) Require(consumed[i]);
    if (plan.operations[i].kind == EvaluationKind::kLogicalValue ||
        FieldValue(graph, plan, OpId{static_cast<std::uint32_t>(i)}) || CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)})) Require(consumed[i]);
  }
}
}
