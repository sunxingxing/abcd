#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_evaluation.h"

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool condition) { if (!condition) throw EvaluationError::kOwnership; }
struct Interval { std::size_t count = 0, end = 0; };
bool Inputs(EvaluationKind kind) {
  return kind == EvaluationKind::kRoot || kind == EvaluationKind::kInline ||
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
std::optional<OpId> Producer(const ValueProgram& graph, const SelectionRegion& region, const OperandUse& use) {
  const auto* value = std::get_if<ValueId>(&use.operand);
  if (!value) return {};
  if (*value == region.merged) return region.branch;
  const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
  return result ? std::optional{result->op} : std::nullopt;
}
struct SelectedObject { OpId allocation, definition; UseId consumer; };
// Index once: each admitted object has exactly one selected definition. This
// also bounds use-chain work across a function with many staged fields.
std::vector<std::optional<OpId>> ObjectDefinitions(const ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<OpId>> result;
  if (!plan.literals) return result;
  budget.Array(result, graph.ops.size());
  for (const auto& object : plan.literals->objects) {
    budget.Step();
    if (object.form == LiteralForm::kStatements && object.count == 1)
      result[object.allocation.value] = plan.literals->definitions[object.first];
  }
  return result;
}
std::optional<SelectedObject> ObjectConsumer(const ValueProgram& graph, const EvaluationPlan& plan,
    const SelectionRegion& region, UseId consumer, std::span<const std::optional<OpId>> definitions, TreeCheckBudget& budget) {
  const auto definition = std::get<OpId>(graph.uses[consumer.value].owner);
  const auto& field = graph.ops[definition.value];
  if (!IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, definition) ||
      !std::holds_alternative<ValueOwnWrite>(field.operation) || field.use_count != 2 ||
      consumer.value != field.first_use.value + 1) return {};
  const auto* base = std::get_if<ValueId>(&graph.uses[field.first_use.value].operand);
  if (!base) return {};
  const auto* result = std::get_if<OpResult>(&graph.values[base->value].origin);
  if (!result || definitions.empty() || definitions[result->op.value] != definition ||
      graph.ops[result->op.value].block != region.header) return {};
  const auto* object = std::get_if<ValueObjectAllocation>(&graph.ops[result->op.value].operation);
  if (!object || !object->literal) return {};
  std::optional<UseId> external;
  for (auto at = graph.values[base->value].first_use; at; at = graph.uses[at->value].next) {
    budget.Step(); const auto& use = graph.uses[at->value];
    if (use.life != ValueLife::kLive || *at == field.first_use) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (external || !owner || graph.ops[owner->value].block != region.join ||
        graph.ops[owner->value].order <= field.order) return {};
    const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
    if (!shape || shape->NeedsSnapshot(use.role) || use.role == OperandRole::kReceiver ||
        PredicateSnapshot(graph, graph.ops[owner->value], use.operand)) return {};
    external = *at;
  }
  if (!external) return {};
  return SelectedObject{result->op, definition, *external};
}
// A header, one atomic conditional value, and its straight join share an event
// sequence. RHS events never become unconditional: their exact subtree remains
// checked in its original arm. Distinct selected domains cannot overlap.
// A private buffered object can enclose that sequence: combine its allocation,
// selected RHS and own definition, then consume the whole interval exactly once.
// Both the original local forest and the projected forward interval are checked;
// constants additionally certify writable own data keys at tree construction.
class Domain {
 public:
  Domain(const ValueProgram& graph, const EvaluationPlan& plan, const SelectionRegion& region,
      UseId consumer, std::vector<Interval>& intervals, TreeCheckBudget& budget,
      std::optional<SelectedObject> object = {})
      : graph_(graph), plan_(plan), region_(region), consumer_(consumer),
        owner_(std::get<OpId>(graph.uses[consumer.value].owner)), intervals_(intervals), budget_(budget), object_(object) {}
  bool Build(std::vector<std::pair<OpId, UseId>>& selected) {
    std::size_t serial = 0;
    bool object_consumed = false;
    for (const auto block : {region_.header, region_.join}) {
      const auto& range = graph_.blocks[block.value];
      for (std::uint32_t j = 0; j < range.op_count; ++j) {
        budget_.Step(); const OpId id{range.first_op.value + j}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        const auto count = Events(op, plan_.operations[id.value], id, region_.branch);
        intervals_[id.value] = {count, serial + count};
        auto expected = serial;
        for (auto ordinal = op.use_count; (Inputs(plan_.operations[id.value].kind) || (object_ && id == object_->definition)) && ordinal != 0; --ordinal) {
          budget_.Step(); const UseId use{op.first_use.value + ordinal - 1};
          const auto& occurrence = graph_.uses[use.value];
          if (IsMemberReceiver(graph_, plan_, occurrence)) continue;
          const auto producer = Producer(graph_, region_, occurrence);
          if (!producer || !Inside(*producer)) continue;
          const auto& placement = plan_.operations[producer->value];
          const bool logical = *producer == region_.branch && use == consumer_;
          const bool base = object_ && id == object_->definition && *producer == object_->allocation;
          const bool literal = object_ && use == object_->consumer && *producer == object_->allocation;
          const bool owned = (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kLogicalLeft) &&
              placement.consumer == use;
          const auto shape = DescribeEvaluation(op.operation);
          const bool candidate = (id == owner_ || (object_ && id == std::get<OpId>(graph_.uses[object_->consumer.value].owner))) && graph_.ops[producer->value].block == region_.header &&
              placement.kind == EvaluationKind::kRoot && placement.temporary == TemporaryReason::kCrossRegionValue &&
              shape && !shape->NeedsSnapshot(occurrence.role) && occurrence.role != OperandRole::kReceiver &&
              !PredicateSnapshot(graph_, op, occurrence.operand);
          if (!owned && !logical && !candidate && !base && !literal) continue;
          const auto interval = intervals_[producer->value];
          if (interval.count && interval.end != expected) {
            if (owned || logical || base || literal) return false;
            continue;
          }
          Require(interval.count <= expected);
          expected -= interval.count; intervals_[id.value].count += interval.count;
          if (candidate && !base && !literal) selected.emplace_back(*producer, use);
          if (literal) object_consumed = true;
        }
        if (object_ && id == object_->definition) intervals_[object_->allocation.value] = intervals_[id.value];
        serial += count;
      }
    }
    return !object_ || object_consumed;
  }
  void Verify(std::vector<std::uint8_t>& consumed) {
    std::size_t serial = 0;
    for (const auto block : {region_.header, region_.join}) {
      const auto& range = graph_.blocks[block.value];
      for (std::uint32_t j = 0; j < range.op_count; ++j) {
        budget_.Step(); const OpId id{range.first_op.value + j}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        const auto count = Events(op, plan_.operations[id.value], id, region_.branch);
        intervals_[id.value] = {count, serial + count};
        std::optional<Interval> children;
        for (std::uint32_t ordinal = 0; (Inputs(plan_.operations[id.value].kind) || (object_ && id == object_->definition)) && ordinal < op.use_count; ++ordinal) {
          budget_.Step(); const UseId use{op.first_use.value + ordinal};
          const auto& occurrence = graph_.uses[use.value];
          if (IsMemberReceiver(graph_, plan_, occurrence)) continue;
          const auto producer = Producer(graph_, region_, occurrence);
          if (!producer || !Inside(*producer)) continue;
          const auto& placement = plan_.operations[producer->value];
          if (placement.kind != EvaluationKind::kInline && placement.kind != EvaluationKind::kLogicalLeft &&
              placement.kind != EvaluationKind::kLogicalValue) continue;
          const bool base = object_ && id == object_->definition && *producer == object_->allocation;
          const bool literal = object_ && use == object_->consumer && *producer == object_->allocation;
          Require(base || placement.consumer == use);
          if (!base && (placement.kind == EvaluationKind::kLogicalValue || CrossLogicalInline(graph_, plan_, *producer))) {
            Require((id == owner_ || (object_ && id == std::get<OpId>(graph_.uses[object_->consumer.value].owner))) && graph_.ops[producer->value].block == region_.header && !consumed[producer->value]);
            const auto shape = DescribeEvaluation(op.operation);
            Require(shape && !shape->NeedsSnapshot(occurrence.role) && occurrence.role != OperandRole::kReceiver &&
                !PredicateSnapshot(graph_, op, occurrence.operand));
            consumed[producer->value] = 1;
          }
          if (literal) Require(placement.kind == EvaluationKind::kInline);
          const auto interval = intervals_[producer->value];
          Require(interval.count <= interval.end);
          if (!interval.count) continue;
          if (children) {
            Require(children->end == interval.end - interval.count);
            children->count += interval.count; children->end = interval.end;
          } else children = interval;
        }
        if (children) { Require(children->end == serial); intervals_[id.value].count += children->count; }
        if (object_ && id == object_->definition) {
          Require(!consumed[id.value]); consumed[id.value] = 1;
          intervals_[object_->allocation.value] = intervals_[id.value];
        }
        serial += count;
      }
    }
  }
 private:
  bool Inside(OpId id) const { const auto block = graph_.ops[id.value].block; return block == region_.header || block == region_.join; }
  const ValueProgram& graph_;
  const EvaluationPlan& plan_;
  const SelectionRegion& region_;
  UseId consumer_;
  OpId owner_;
  std::vector<Interval>& intervals_;
  TreeCheckBudget& budget_;
  std::optional<SelectedObject> object_;
};
bool Available(const ValueProgram& graph, const SelectionRegion& region, const std::vector<std::uint8_t>& used) {
  // The forest lowers definitions before join consumers. Keep arbitrary CFG
  // layout support in the ordinary phi route until a dependency worklist owns it.
  if (!region.complete_phi || region.parent || used[region.header.value] || used[region.join.value]) return false;
  const auto join = graph.blocks[region.join.value].first_op.value;
  if (region.last_dependency) return region.last_dependency->value < join;
  return region.branch.value < join && graph.blocks[region.evaluate.value].first_op.value < join;
}
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
  if (!plan.logical_objects.empty() && plan.logical_objects[operation])
    return {EvaluationKind::kRoot, {}, TemporaryReason::kMultipleUses};
  if (placement.kind == EvaluationKind::kInitializer &&
      IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, OpId{static_cast<std::uint32_t>(operation)}))
    return {EvaluationKind::kRoot, {}, {}};
  if (placement.kind == EvaluationKind::kLogicalValue) return {EvaluationKind::kRoot, {}, {}};
  if (CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(operation)}))
    return {EvaluationKind::kRoot, {}, TemporaryReason::kCrossRegionValue};
  return placement;
}
void BuildLogicalValues(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  const auto regions = Regions(graph, plan, budget);
  if (regions.empty()) return;
  const auto definitions = ObjectDefinitions(graph, plan, budget);
  std::vector<Interval> intervals; std::vector<std::uint8_t> used; std::vector<std::pair<OpId, UseId>> selected;
  budget.Array(intervals, graph.ops.size()); budget.Array(used, graph.blocks.size());
  budget.Admit(graph.ops.size(), sizeof(std::pair<OpId, UseId>)); selected.reserve(graph.ops.size());
  budget.Admit(selected.capacity() - graph.ops.size(), sizeof(std::pair<OpId, UseId>));
  for (const auto& region : regions) {
    budget.Step(); if (!Available(graph, region, used)) continue;
    const auto consumer = Consumer(graph, region, budget);
    if (!consumer) continue;
    selected.clear();
    auto object = ObjectConsumer(graph, plan, region, *consumer, definitions, budget);
    if (object && !Domain(graph, plan, region, *consumer, intervals, budget, object).Build(selected)) {
      object.reset(); selected.clear();
    }
    if (!object && !Domain(graph, plan, region, *consumer, intervals, budget).Build(selected)) continue;
    if (object) {
      if (plan.logical_objects.empty()) budget.Array(plan.logical_objects, graph.ops.size());
      plan.logical_objects[object->allocation.value] = object->definition;
      plan.operations[object->allocation.value] = {EvaluationKind::kInline, object->consumer, {}};
      plan.operations[object->definition.value] = {EvaluationKind::kInitializer, {}, {}};
    }
    if (plan.logical_values.empty()) budget.Array(plan.logical_values, graph.values.size());
    plan.logical_values[region.merged.value] = region.branch;
    plan.operations[region.branch.value] = {EvaluationKind::kLogicalValue, consumer, {}};
    for (const auto& [producer, use] : selected) {
      budget.Step(); plan.operations[producer.value] = {EvaluationKind::kInline, use, {}};
    }
    used[region.header.value] = used[region.join.value] = 1;
  }
}
void VerifyLogicalValues(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  Require(plan.logical_objects.empty() || plan.logical_objects.size() == graph.ops.size());
  Require(plan.logical_values.empty() || (plan.flow == EvaluationFlow::kNormal && plan.logical_values.size() == graph.values.size()));
  if (plan.logical_values.empty()) {
    Require(plan.logical_objects.empty());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); Require(plan.operations[i].kind != EvaluationKind::kLogicalValue &&
          !(plan.operations[i].kind == EvaluationKind::kInitializer &&
            IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, OpId{static_cast<std::uint32_t>(i)})) &&
          !CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)}));
    }
    return;
  }
  const auto regions = Regions(graph, plan, budget);
  const auto definitions = ObjectDefinitions(graph, plan, budget);
  std::vector<Interval> intervals; std::vector<std::uint8_t> used, consumed, values, objects;
  budget.Array(intervals, graph.ops.size()); budget.Array(used, graph.blocks.size());
  budget.Array(consumed, graph.ops.size()); budget.Array(values, graph.values.size());
  budget.Array(objects, graph.ops.size());
  for (const auto& region : regions) {
    budget.Step(); if (region.parent || !plan.logical_values[region.merged.value]) continue;
    Require(plan.logical_values[region.merged.value] == region.branch && Available(graph, region, used));
    const auto consumer = Consumer(graph, region, budget);
    const auto& placement = plan.operations[region.branch.value];
    Require(consumer && placement.kind == EvaluationKind::kLogicalValue && placement.consumer == consumer && !placement.temporary);
    auto object = ObjectConsumer(graph, plan, region, *consumer, definitions, budget);
    if (object && (plan.logical_objects.empty() || !plan.logical_objects[object->allocation.value])) object.reset();
    if (object) {
      objects[object->allocation.value] = 1;
      Require(plan.operations[object->allocation.value].kind == EvaluationKind::kInline &&
          plan.operations[object->allocation.value].consumer == object->consumer &&
          !plan.operations[object->allocation.value].temporary);
      Require(plan.logical_objects[object->allocation.value] == object->definition &&
          plan.operations[object->definition.value].kind == EvaluationKind::kInitializer &&
          !plan.operations[object->definition.value].consumer && !plan.operations[object->definition.value].temporary);
    }
    Domain(graph, plan, region, *consumer, intervals, budget, object).Verify(consumed);
    values[region.merged.value] = 1;
    used[region.header.value] = used[region.join.value] = 1;
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
        CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)})) Require(consumed[i]);
  }
}
}
