#include "logical_evaluation.h"
#include "evaluation_failure.h"
#include "evaluation_aliases.h"
#include "logical_regions.h"
#include "selection_consumer.h"
#include "phase_evaluation.h"
#include "call_plan.h"
#include "module_evaluation.h"
#include <bit>

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool condition) { if (!condition) throw EvaluationError::kOwnership; }
struct Interval { std::size_t count = 0, end = 0; };
bool Inputs(EvaluationKind kind) {
  return kind == EvaluationKind::kRoot || kind == EvaluationKind::kInline || kind == EvaluationKind::kPredicateSnapshot ||
      kind == EvaluationKind::kLogicalLeft || kind == EvaluationKind::kLogicalValue || kind == EvaluationKind::kPhiInput;
}
std::vector<SelectionRegion> Regions(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget, std::span<const JointSelectionUse> joints = {}) {
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
    const auto error = ControlResultError(built);
    if (error == ControlError::kResourceLimit) throw EvaluationError::kResourceLimit;
    if (error == ControlError::kWorkLimit) throw EvaluationError::kWorkLimit;
    return {};
  }
  budget.Admit(control->allocated_bytes, 1); budget.Step(control->work);
  return FindSelectionExpressions(graph, *control, plan, budget, nullptr, joints);
}
std::size_t Events(const ValueOp& op, const OpEvaluation& placement, OpId id, OpId branch) {
  if (id == branch) return 1; // One conditional region; RHS is independently owned/checked.
  if (placement.kind == EvaluationKind::kCellGuard || placement.kind == EvaluationKind::kModuleGuard) return 0;
  if (std::holds_alternative<ValueModuleRead>(op.operation)) return 2;
  const auto shape = DescribeEvaluation(op.operation);
  // The corridor owns a pure control transfer as an anchor, not a language
  // effect. Keeping it on the effect clock would make block splitting change
  // expression ownership. Conditional transfers remain atomic events above.
  if (op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible && shape &&
      shape->control == ControlEvaluation::kNormal && shape->Count() == 0) return 0;
  return OrderedEvaluation(op) || IsEnvironmentState(op.operation);
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
struct ObjectRange {
  std::size_t first_object = 0, object_count = 0, first_field = 0, field_count = 0;
};
struct SelectionIndex {
  std::vector<std::optional<std::size_t>> headers, objects;
  std::vector<SelectionConsumer> consumers;
  std::vector<std::optional<OpId>> fields, expressions;
  std::vector<std::optional<std::size_t>> field_regions;
  std::vector<std::optional<std::size_t>> continuations;
  std::vector<std::optional<OpId>> selection_fields;
  std::vector<std::optional<OpId>> first_selections;
  std::vector<std::uint8_t> ambiguous_fields;
  std::vector<std::optional<UseId>> object_consumers;
  std::vector<std::optional<OpId>> object_roots;
  std::vector<ObjectRange> groups;
  std::vector<OpId> group_objects, group_fields;
};
struct CanonicalUses {
  std::vector<std::optional<UseId>> heads, next;
  CanonicalUses(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
    budget.Array(heads, graph.values.size()); budget.Array(next, graph.uses.size());
    // Checked identity transports are not observations. Index their eventual
    // users at the common alias root, so copies cannot hide an early escape or
    // invent an extra consumer. Each live occurrence is indexed once.
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step(); const auto& use = graph.uses[i];
      if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan, use)) continue;
      const auto* owner = std::get_if<OpId>(&use.owner);
      if (owner && (plan.operations[owner->value].kind == EvaluationKind::kAlias ||
          plan.operations[owner->value].kind == EvaluationKind::kElidedPure)) continue;
      const auto operand = ResolveEvaluationOperand(plan, use.operand);
      const auto* value = std::get_if<ValueId>(&operand); if (!value) continue;
      next[i] = heads[value->value]; heads[value->value] = UseId{static_cast<std::uint32_t>(i)};
    }
  }
};
std::optional<UseId> ObjectConsumer(const ValueProgram& graph, const ObjectInit& object,
    const LiteralPlan& literals, const SelectionIndex& index, const CanonicalUses& uses, TreeCheckBudget& budget) {
  if (!object.count) return {};
  const auto last = literals.definitions[object.first + object.count - 1];
  std::optional<UseId> external;
  for (auto at = uses.heads[graph.ops[object.allocation.value].result->value];
       at; at = uses.next[at->value]) {
    budget.Step(); const auto& use = graph.uses[at->value];
    if (use.life != ValueLife::kLive) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    if (owner && index.fields[owner->value] == object.allocation && *at == graph.ops[owner->value].first_use) continue;
    if (external || !owner || *owner <= last) return {};
    const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
    if (!shape || shape->NeedsSnapshot(use.role) || use.role == OperandRole::kReceiver ||
        PredicateSnapshot(graph, graph.ops[owner->value], use.operand)) return {};
    external = *at;
  }
  return external;
}
void IndexObjectGroups(const ValueProgram& graph, const EvaluationPlan& plan,
    SelectionIndex& index, TreeCheckBudget& budget) {
  if (!plan.literals) return;
  const CanonicalUses uses(graph, plan, budget);
  budget.Array(index.object_consumers, graph.ops.size()); budget.Array(index.object_roots, graph.ops.size());
  budget.Array(index.groups, graph.ops.size());
  budget.Array(index.group_objects, plan.literals->objects.size());
  budget.Array(index.group_fields, plan.literals->definitions.size());
  // Parent allocations precede child allocations. Resolve each ownership edge
  // once, rather than walking every child's ancestor chain. Both results need
  // a sole external consumer; the edge must be a parent's actual stored RHS.
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!index.objects[i]) continue;
    const auto& object = plan.literals->objects[*index.objects[i]];
    const auto use = ObjectConsumer(graph, object, *plan.literals, index, uses, budget);
    index.object_consumers[i] = use; index.object_roots[i] = object.allocation;
    if (use) {
      const auto& occurrence = graph.uses[use->value]; const auto owner = std::get<OpId>(occurrence.owner);
      const auto parent = index.fields[owner.value];
      if (parent && parent->value < i && occurrence.role == OperandRole::kStoredValue &&
          index.object_consumers[parent->value]) index.object_roots[i] = index.object_roots[parent->value];
    }
    auto& group = index.groups[index.object_roots[i]->value];
    ++group.object_count; group.field_count += object.count;
  }
  std::size_t objects = 0, fields = 0;
  std::vector<std::size_t> next; budget.Array(next, graph.ops.size());
  for (std::size_t i = 0; i < index.groups.size(); ++i) {
    budget.Step(); auto& group = index.groups[i];
    group.first_object = objects; group.first_field = fields;
    next[i] = objects; objects += group.object_count; fields += group.field_count;
  }
  Require(objects <= index.group_objects.size() && fields <= index.group_fields.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (index.object_roots[i])
      index.group_objects[next[index.object_roots[i]->value]++] = OpId{static_cast<std::uint32_t>(i)};
  }
  for (std::size_t i = 0; i < index.groups.size(); ++i) { budget.Step(); next[i] = index.groups[i].first_field; }
  // One operation-order pass merges every group's definitions, including
  // interleaved child and parent fields. No per-root sort or descendant scan.
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (index.fields[i])
      index.group_fields[next[index.object_roots[index.fields[i]->value]->value]++] = OpId{static_cast<std::uint32_t>(i)};
  }
}
SelectionIndex IndexSelections(const ValueProgram& graph, const EvaluationPlan& plan,
    std::span<const SelectionRegion> regions, TreeCheckBudget& budget, std::span<const JointSelectionUse> joints = {}) {
  SelectionIndex index;
  budget.Array(index.headers, graph.blocks.size()); budget.Array(index.objects, graph.ops.size());
  budget.Array(index.consumers, graph.ops.size()); budget.Array(index.fields, graph.ops.size());
  budget.Array(index.expressions, graph.ops.size());
  budget.Array(index.field_regions, graph.ops.size()); budget.Array(index.first_selections, graph.ops.size());
  budget.Array(index.continuations, graph.ops.size());
  budget.Array(index.selection_fields, graph.ops.size());
  budget.Array(index.ambiguous_fields, graph.ops.size());
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); const auto& region = regions[i];
    if (region.parent || !region.complete_phi) continue;
    Require(!index.headers[region.header.value]); index.headers[region.header.value] = i;
  }
  std::vector<WorkBlockId> terminals;
  if(!joints.empty()) {
  // Empty sole-entry corridors preserve the occurrence and execution count;
  // block splitting must not manufacture a materialization boundary.
  std::vector<std::optional<WorkBlockId>> next_header; std::vector<WorkBlockId> header_path;
  std::vector<std::uint8_t> header_state;
  budget.Array(terminals,graph.blocks.size());
  budget.Array(next_header,graph.blocks.size());budget.Array(header_path,graph.blocks.size());budget.Array(header_state,graph.blocks.size());
  std::vector<std::size_t> incoming;budget.Array(incoming,graph.blocks.size());
  for(const auto& edge:graph.edges){budget.Step();++incoming[edge.target.value];}
  for(std::size_t b=0;b<graph.blocks.size();++b) {
    budget.Step();const auto& block=graph.blocks[b];if(index.headers[b] || block.edge_count!=1)continue;
    const auto& edge=graph.edges[block.first_edge.value];if(incoming[edge.target.value]!=1)continue;
    bool empty=true;for(std::uint32_t j=0;j<block.op_count;++j){budget.Step();const auto id=block.first_op.value+j;const auto& op=graph.ops[id];
      if(op.life==ValueLife::kLive && plan.operations[id].kind!=EvaluationKind::kAlias && plan.operations[id].kind!=EvaluationKind::kElidedPure &&
          !std::holds_alternative<ValueJump>(op.operation))empty=false;}
    if(empty)next_header[b]=edge.target;
  }
  for(std::size_t b=0;b<graph.blocks.size();++b) {
    budget.Step();if(header_state[b]==2)continue;WorkBlockId at{static_cast<std::uint32_t>(b)};std::size_t count=0;
    while(!header_state[at.value]){budget.Step();header_state[at.value]=1;header_path[count++]=at;if(!next_header[at.value])break;at=*next_header[at.value];}
    const auto header=(!next_header[at.value] || header_state[at.value]==2)?index.headers[at.value]:std::nullopt;
    const auto terminal=header_state[at.value]==2?terminals[at.value]:at;
    while(count){budget.Step();const auto id=header_path[--count];index.headers[id.value]=header;terminals[id.value]=terminal;header_state[id.value]=2;}
  }
  }
  for (const auto& region : regions) {
    budget.Step(); if (region.parent || !region.complete_phi) continue;
    const auto next = index.headers[region.join.value];
    const auto* logical_left = next && !regions[*next].consequent && regions[*next].left == region.merged ? &regions[*next] : nullptr;
    index.consumers[region.branch.value] = FindSelectionConsumer(graph, region, budget, logical_left);
    auto& consumer=index.consumers[region.branch.value];
    if(!joints.empty() && consumer.reason==SelectionExitReason::kCrossBlock && consumer.witness && !consumer.use) {
      const auto owner=std::get<OpId>(graph.uses[consumer.witness->value].owner);
      if(terminals[region.join.value]==graph.ops[owner.value].block) {
        // Recount all observations; a witness alone does not prove uniqueness.
        std::size_t count=0;for(auto at=graph.values[region.merged.value].first_use;at;at=graph.uses[at->value].next){budget.Step();if(graph.uses[at->value].life==ValueLife::kLive)++count;}
        const auto shape=DescribeEvaluation(graph.ops[owner.value].operation);const auto& use=graph.uses[consumer.witness->value];
        if(count==1 && shape && !shape->NeedsSnapshot(use.role) && use.role!=OperandRole::kReceiver && !PredicateSnapshot(graph,graph.ops[owner.value],use.operand))
          consumer={SelectionExitKind::kUnique,SelectionExitReason::kNone,consumer.witness,{},{},{}};
      }
    }
  }
  if(!joints.empty()) {
  // Joint discovery is not a generic phi-use exemption. The later region's
  // independently derived truth projection must own every predicate and the
  // exact unchanged keep edge, beginning at this selection's original join.
  std::vector<std::optional<std::size_t>> joint_values; budget.Array(joint_values, graph.values.size());
  for (std::size_t i=0;i<joints.size();++i) {budget.Step();joint_values[joints[i].value.value]=i;}
  std::vector<std::optional<OpId>> truth_owners;budget.Array(truth_owners,graph.ops.size());
  for(const auto& region:regions) if(region.logical) for(const auto op:region.logical->truths) {
    budget.Step();truth_owners[op.value]=region.logical->shared_owner.value_or(region.branch);
  }
  for (const auto& region:regions) {
    budget.Step();if(region.parent || !region.complete_phi || !joint_values[region.merged.value])continue;
    const auto next=index.headers[region.join.value];if(!next)continue;
    const auto& later=regions[*next];const auto& proof=joints[*joint_values[region.merged.value]];
    if(!later.logical || later.logical->value!=region.merged)continue;
    const auto& keep=graph.uses[proof.keep.value];
    const auto incoming=later.logical->shared_keep
        ? std::optional{std::get<EdgeId>(*graph.uses[later.logical->shared_keep->value].incoming)}
        : later.logical->keep_consequent?later.keep_input:later.evaluate_input;
    const auto* edge=keep.incoming?std::get_if<EdgeId>(&*keep.incoming):nullptr;
    if(keep.owner!=decltype(keep.owner){later.merged} || !incoming || !edge || *incoming!=*edge)continue;
    bool covered=true;
    for(const auto use:proof.predicates) {
      budget.Step();const auto owner=std::get<OpId>(graph.uses[use.value].owner);
      if(truth_owners[owner.value]!=later.branch)covered=false;
    }
    if(!covered || !std::binary_search(proof.predicates.begin(),proof.predicates.end(),later.logical->input))continue;
    index.consumers[region.branch.value]={SelectionExitKind::kPredicateKeep,SelectionExitReason::kNone,later.logical->input,{},proof.keep,*edge};
  }
  }
  if (plan.literals) for (std::size_t i = 0; i < plan.literals->objects.size(); ++i) {
    budget.Step(); const auto& object = plan.literals->objects[i];
    if (object.form != LiteralForm::kStatements) continue;
    index.objects[object.allocation.value] = i;
    for (std::uint32_t j = 0; j < object.count; ++j) {
      budget.Step(); index.fields[plan.literals->definitions[object.first + j].value] = object.allocation;
    }
  }
  IndexObjectGroups(graph, plan, index, budget);
  std::vector<OpId> expression_roots; budget.Array(expression_roots, graph.ops.size());
  for (std::size_t i = graph.ops.size(); i-- > 0;) {
    budget.Step(); expression_roots[i] = OpId{static_cast<std::uint32_t>(i)};
    const auto placement = BeforeLogicalFusion(graph, plan, i);
    if ((placement.kind != EvaluationKind::kInline && placement.kind != EvaluationKind::kLogicalLeft) || !placement.consumer) continue;
    const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
    if (owner && owner->value > i && graph.ops[owner->value].block == graph.ops[i].block)
      expression_roots[i] = expression_roots[owner->value];
  }
  for (const auto& region : regions) {
    budget.Step(); const auto use = index.consumers[region.branch.value].InlineUse();
    const auto next = index.headers[region.join.value];
    if (!use || !next) continue;
    const auto owner = std::get<OpId>(graph.uses[use->value].owner);
    if (expression_roots[owner.value] == regions[*next].branch && regions[*next].branch > region.branch)
      index.continuations[region.branch.value] = next;
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
        if (index.expressions[result->op.value]) {
          auto evidence = EvaluationAt(graph, plan, result->op, EvaluationObligation::kProducerOwnership);
          evidence.expected_consumer = use; evidence.expected_owner = OperationLocation(graph, {static_cast<std::uint32_t>(i)});
          throw EvaluationFailure{EvaluationError::kOwnership, evidence};
        }
        index.expressions[result->op.value] = index.expressions[i];
      }
    }
  }
  // Only the first selected field can start an object's route. Index this once
  // so ordinary prefix/suffix fields do not cause a whole-object retry at each
  // later selection. Multiple selections within one RHS need another domain.
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); const auto& region = regions[i];
    const auto use = index.consumers[region.branch.value].InlineUse();
    if (!use) continue;
    const auto owner = std::get<OpId>(graph.uses[use->value].owner);
    const auto field = index.expressions[owner.value];
    if (!field || !index.fields[field->value]) continue;
    index.selection_fields[region.branch.value] = field;
    if (index.field_regions[field->value]) index.ambiguous_fields[field->value] = 1;
    else index.field_regions[field->value] = i;
  }
  // A selected value can feed the next selector's predicate before the field
  // receives the final value. Propagate the field owner backwards once.
  for (std::size_t i = graph.ops.size(); i-- > 0;) {
    budget.Step();
    if (!index.selection_fields[i] && index.continuations[i])
      index.selection_fields[i] = index.selection_fields[regions[*index.continuations[i]].branch.value];
    const auto field = index.selection_fields[i];
    if (!field) continue;
    auto& first = index.first_selections[index.object_roots[index.fields[field->value]->value]->value];
    first = OpId{static_cast<std::uint32_t>(i)};
  }
  return index;
}
std::vector<std::size_t> SelectionOrder(const ValueProgram& graph, std::span<const SelectionRegion> regions,
    TreeCheckBudget& budget) {
  std::vector<std::optional<std::size_t>> branches;
  std::vector<std::size_t> order, roots, depths;
  budget.Array(branches, graph.ops.size()); budget.Array(order, regions.size());
  budget.Array(roots, regions.size()); budget.Array(depths, regions.size());
  for (std::size_t i = 0; i < regions.size(); ++i) { budget.Step(); branches[regions[i].branch.value] = i; order[i] = i; }
  // Resolve the containment forest once, then preserve
  // operation order between independent roots while preparing prefix values
  // before their containing decision. No whole-graph fixed-point rescans.
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<Visit> state;
  std::vector<std::size_t> path;
  budget.Array(state, regions.size()); budget.Array(path, regions.size());
  const auto parent = [&](std::size_t i) -> std::optional<std::size_t> {
    const auto branch = regions[i].prefix_parent ? regions[i].prefix_parent : regions[i].parent;
    if (!branch) return {};
    Require(branches[branch->value].has_value()); return branches[branch->value];
  };
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); if (state[i] == Visit::kDone) continue;
    auto current = i; std::size_t count = 0;
    while (state[current] != Visit::kDone) {
      budget.Step(); Require(state[current] == Visit::kUnseen); state[current] = Visit::kActive; path[count++] = current;
      const auto next = parent(current);
      if (!next) { roots[current] = regions[current].branch.value; state[current] = Visit::kDone; --count; break; }
      current = *next;
    }
    while (count) {
      budget.Step(); const auto child = path[--count], p = *parent(child);
      roots[child] = roots[p]; depths[child] = depths[p] + 1; state[child] = Visit::kDone;
    }
  }
  std::sort(order.begin(), order.end(), [&](auto a, auto b) {
    budget.Step();
    if (roots[a] != roots[b]) return roots[a] < roots[b];
    if (depths[a] != depths[b]) return depths[a] > depths[b];
    return regions[a].branch < regions[b].branch;
  });
  return order;
}
struct SelectedObject { OpId allocation; std::span<const OpId> allocations, definitions; UseId consumer; };
// Follow adjacent complete selections and intervening ordinary definitions in
// original field order. Only the first-selection candidate visits the object's
// fields/use chain; no per-field whole-object or whole-CFG search is needed.
std::optional<SelectedObject> ObjectRoute(const ValueProgram& graph, const EvaluationPlan& plan,
    std::span<const SelectionRegion> regions, const SelectionIndex& index,
    const std::vector<SelectionBlock>& used, std::vector<const SelectionRegion*>& route, TreeCheckBudget& budget) {
  const auto first_use = index.consumers[route.front()->branch.value].InlineUse();
  if (!first_use || !plan.literals) return {};
  if (!index.selection_fields[route.front()->branch.value]) return {};
  const auto first = *index.selection_fields[route.front()->branch.value];
  if (!index.fields[first.value]) return {};
  const auto allocation = *index.object_roots[index.fields[first.value]->value];
  const auto& group = index.groups[allocation.value];
  const auto definitions = std::span(index.group_fields).subspan(group.first_field, group.field_count);
  const auto allocations = std::span(index.group_objects).subspan(group.first_object, group.object_count);
  // Domain owns availability, execution count and effect order, including the
  // common linear prefix. Allocation/header block identity proves none of them.
  if (definitions.empty() || index.first_selections[allocation.value] != route.front()->branch) return {};
  const auto storage = [&](OpId id) {
    const auto* object =
        std::get_if<ValueObjectAllocation>(&graph.ops[id.value].operation);
    return (object && object->literal) ||
           (index.objects[id.value] &&
            plan.literals->objects[*index.objects[id.value]].indexed_first);
  };
  if (!storage(allocation)) return {};
  auto block = route.front()->header;
  bool selected = false;
  for (const auto definition : definitions) {
    budget.Step();
    if (index.ambiguous_fields[definition.value]) return {};
    const auto& field = graph.ops[definition.value];
    while (field.block != block) {
      budget.Step(); const auto next = index.headers[block.value];
      if (!next) return {};
      const auto& region = regions[*next];
      if (index.selection_fields[region.branch.value] != definition || !Available(graph, region, used)) return {};
      if (selected) {
        if (region.branch <= route.back()->branch) return {};
        route.push_back(&region);
      } else if (&region != route.front()) return {};
      selected = true; block = region.join;
    }
    const auto owner = index.fields[definition.value];
    const bool indexed =
        owner && index.objects[owner->value] &&
        plan.literals->objects[*index.objects[owner->value]].indexed_first &&
        std::holds_alternative<ValueIndexedWrite>(field.operation);
    if (field.use_count != 2 ||
        (!std::holds_alternative<ValueOwnWrite>(field.operation) && !indexed) ||
        field.block != block)
      return {};
  }
  if (!selected) return {};
  const auto external = index.object_consumers[allocation.value];
  if (!external) return {};
  const auto owner = std::get<OpId>(graph.uses[external->value].owner);
  if (graph.ops[owner.value].block != route.back()->join || owner <= definitions.back()) return {};
  for (const auto id : allocations) {
    budget.Step();
    if (!storage(id) || !index.object_consumers[id.value]) return {};
  }
  return SelectedObject{allocation, allocations, definitions, *external};
}
struct DomainIndex {
  std::vector<std::uint8_t> blocks, owners, needed;
  std::vector<OpId> needed_ops;
  std::vector<WorkBlockId> order;
  std::vector<std::uint32_t> starts;
  std::vector<std::optional<WorkBlockId>> previous, corridor;
  std::vector<std::size_t> position;
  std::vector<std::optional<OpId>> values;
  std::vector<std::optional<UseId>> branches;
  DomainIndex(const ValueProgram& graph, TreeCheckBudget& budget) {
    budget.Array(blocks, graph.blocks.size()); budget.Array(owners, graph.ops.size());
    budget.Array(values, graph.values.size()); budget.Array(branches, graph.ops.size());
    budget.Array(needed, graph.ops.size()); budget.Array(needed_ops, graph.ops.size()); needed_ops.clear();
    budget.Array(order, graph.blocks.size()); order.clear();
    budget.Array(starts, graph.blocks.size()); budget.Array(previous, graph.blocks.size());
    budget.Array(corridor, graph.blocks.size()); budget.Array(position, graph.blocks.size());
    std::vector<std::size_t> incoming;
    std::vector<std::optional<WorkBlockId>> next;
    std::vector<std::uint8_t> predecessors, visited;
    budget.Array(incoming, graph.blocks.size()); budget.Array(next, graph.blocks.size());
    budget.Array(predecessors, graph.blocks.size()); budget.Array(visited, graph.blocks.size());
    for (const auto& edge : graph.edges) { budget.Step(); ++incoming[edge.target.value]; }
    // A sole ordinary successor and sole incoming edge prove equal execution
    // counts along a linear corridor. Edge polarity and physical block order
    // do not define it. Resume/exception transfers and closed cycles stay out.
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      budget.Step(); const auto& block = graph.blocks[b];
      if (block.edge_count != 1) continue;
      const auto& edge = graph.edges[block.first_edge.value];
      if ((edge.kind != FlowEdgeKind::kTaken && edge.kind != FlowEdgeKind::kFallthrough) ||
          incoming[edge.target.value] != 1) continue;
      next[b] = edge.target; predecessors[edge.target.value] = 1;
    }
    for (std::size_t root = 0; root < graph.blocks.size(); ++root) {
      budget.Step(); if (predecessors[root]) continue;
      auto block = WorkBlockId{static_cast<std::uint32_t>(root)};
      std::size_t rank = 0;
      for (;;) {
        budget.Step(); Require(!visited[block.value]); visited[block.value] = 1;
        corridor[block.value] = WorkBlockId{static_cast<std::uint32_t>(root)}; position[block.value] = rank++;
        if (!next[block.value]) break;
        const auto following = *next[block.value];
        previous[following.value] = block; block = following;
      }
    }
  }
};
// The selection's consumer may itself be an inline operand of a larger
// expression in the same block (e.g. the argument call of `o.f(G(x?.y))`).
// That whole expression is the transformation's final consumer: its earlier
// operands, such as a member callee read before the selection, belong to the
// same eager evaluation and join the same interval proof. Only actual inline
// consumer edges extend it, forward within the block; any root or other
// placement ends it. Returns the final consumer; collects the ancestors.
OpId InlineAncestors(const ValueProgram& graph, const EvaluationPlan& plan, OpId consumer,
    TreeCheckBudget& budget, std::vector<OpId>* chain = nullptr) {
  for (;;) {
    budget.Step();
    const auto& placement = plan.operations[consumer.value];
    if (placement.kind != EvaluationKind::kInline || !placement.consumer) return consumer;
    const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
    if (!owner || graph.ops[owner->value].block != graph.ops[consumer.value].block ||
        graph.ops[owner->value].order <= graph.ops[consumer.value].order) return consumer;
    consumer = *owner;
    if (chain) { budget.Admit(1, sizeof(OpId)); chain->push_back(consumer); }
  }
}
// A missing consumer is a missing proof, not operation zero. Keep the
// selected branch as the obligation owner; never dereference absent evidence
// while constructing a diagnostic (an empty optional previously leaked PC 0).
OpId LogicalOwner(const ValueProgram& graph, const EvaluationPlan& plan,
    std::optional<UseId> use, OpId branch) {
  auto evidence = EvaluationAt(graph, plan, branch, EvaluationObligation::kLogicalInput);
  evidence.expected_consumer = use;
  if (!use || use->value >= graph.uses.size())
    throw EvaluationFailure{EvaluationError::kOwnership, evidence};
  const auto* owner = std::get_if<OpId>(&graph.uses[use->value].owner);
  if (!owner || owner->value >= graph.ops.size())
    throw EvaluationFailure{EvaluationError::kOwnership, evidence};
  return *owner;
}
// Contract selections with their linear execution prefixes and final join.
// Each selection is one conditional event; its arms remain independently checked. A private
// object interval grows at each own definition and is consumed exactly once.
// Shared header/join blocks are visited once, so fields cannot duplicate effects.
class Domain {
 public:
  Domain(const ValueProgram& graph, const EvaluationPlan& plan, const SelectionIndex& selections,
      std::span<const SelectionRegion* const> route, std::vector<Interval>& intervals,
      DomainIndex& index, const std::vector<SelectionBlock>& used, TreeCheckBudget& budget, std::optional<SelectedObject> object = {}, std::span<const OpId> reference_owners = {}, std::optional<OpId> reference = {})
      : graph_(graph), plan_(plan), selections_(selections), route_(route), intervals_(intervals), index_(index), budget_(budget), object_(object),
        last_owner_(LogicalOwner(graph, plan, object ? std::optional{object->consumer} :
            selections.consumers[route.back()->branch.value].InlineUse(), route.back()->branch)) {
    witness_ = EvaluationAt(graph_, plan_, last_owner_, EvaluationObligation::kLogicalJoin);
    Require(!reference_owners.empty() || graph.ops[last_owner_.value].block == route.back()->join);
    // Object routes keep their literal consumer as the final consumer.
    if (!reference_owners.empty()) {
      budget.Step(reference_owners.size());
      budget.Admit(reference_owners.size(), sizeof(OpId));
      chain_.assign(reference_owners.begin(), reference_owners.end());
      budget.Admit(chain_.capacity() - reference_owners.size(), sizeof(OpId));
      Require(reference.has_value() && !object_);
      last_owner_ = *reference;
    } else if (!object_) {
      InlineAncestors(graph, plan, last_owner_, budget_, &chain_);
      if (!chain_.empty()) last_owner_ = chain_.back();
    }
    // Include every preceding linear block, stopping at the already consumed
    // prefix of an earlier domain. Their effects participate in the same
    // suffix proof as the conditional and final consumer; no layout exception.
    auto block = route.front()->header;
    index_.order.push_back(block); index_.starts[block.value] = 0;
    while (const auto preceding = index_.previous[block.value]) {
      budget_.Step(); if (used[preceding->value].header) break;
      const auto end = used[preceding->value].prefix_end;
      const auto start = end ? graph.ops[end->value].order + 1 : 0;
      if (start < graph.blocks[preceding->value].op_count) {
        index_.order.push_back(*preceding); index_.starts[preceding->value] = start;
      }
      if (end) break;
      block = *preceding;
    }
    std::reverse(index_.order.begin(), index_.order.end());
    for (std::size_t r = 1; r < route.size(); ++r) {
      budget_.Step();
      const auto first = index_.order.size();
      auto at = route[r]->header;
      index_.order.push_back(at); index_.starts[at.value] = 0;
      if (!reference_owners.empty()) {
        // Restore intervening sole-entry normal corridors. This proves split
        // blocks by their edges, never by physical block/operation adjacency.
        while (at != route[r-1]->join) {
          budget_.Step();
          const auto before = index_.previous[at.value];
          Require(before.has_value()); at = *before;
          Require(index_.order.size() < index_.blocks.size());
          index_.order.push_back(at); index_.starts[at.value] = 0;
        }
        std::reverse(index_.order.begin() + first, index_.order.end());
      }
    }
    const auto suffix = index_.order.size();
    auto tail = reference_owners.empty() ? route.back()->join : graph.ops[last_owner_.value].block;
    for (;;) {
      budget_.Step(); Require(index_.order.size() < index_.blocks.size());
      index_.order.push_back(tail); index_.starts[tail.value] = 0;
      if (tail == route.back()->join) break;
      Require(index_.previous[tail.value].has_value()); tail = *index_.previous[tail.value];
    }
    std::reverse(index_.order.begin() + suffix, index_.order.end());
    // Prepay the nonthrowing cleanup as well as index construction.
    budget_.Step(2 * (index_.order.size() + route.size() + (object_ ? object_->definitions.size() : 0)));
    for (const auto id : index_.order) {
      witness_ = {}; witness_.obligation = EvaluationObligation::kDuplicateVisit; witness_.location.block = id;
      Require(!index_.blocks[id.value]); index_.blocks[id.value] = 1;
    }
    for (const auto* region : route) {
      index_.values[region->merged.value] = region->branch;
      index_.branches[region->branch.value] = selections.consumers[region->branch.value].InlineUse();
      const auto owner = std::get<OpId>(graph.uses[index_.branches[region->branch.value]->value].owner);
      index_.owners[owner.value] = 1;
    }
    if (object_) {
      index_.owners[std::get<OpId>(graph.uses[object_->consumer.value].owner).value] = 1;
      for (const auto field : object_->definitions) index_.owners[field.value] = 1;
    }
    for (const auto owner : chain_) { budget_.Step(); index_.owners[owner.value] = 1; }
    Dependencies();
  }
  ~Domain() {
    for (const auto op : index_.needed_ops) index_.needed[op.value] = 0;
    index_.needed_ops.clear();
    for (const auto* region : route_) {
      index_.values[region->merged.value].reset();
      const auto use = *index_.branches[region->branch.value]; index_.branches[region->branch.value].reset();
      index_.owners[std::get<OpId>(graph_.uses[use.value].owner).value] = 0;
    }
    for (const auto block : index_.order) index_.blocks[block.value] = 0;
    index_.order.clear();
    if (object_) {
      index_.owners[std::get<OpId>(graph_.uses[object_->consumer.value].owner).value] = 0;
      for (const auto field : object_->definitions) index_.owners[field.value] = 0;
    }
    for (const auto owner : chain_) index_.owners[owner.value] = 0;
  }
  bool Build(std::vector<std::pair<OpId, UseId>>& selected) {
    std::size_t serial = 0, definition = 0, objects_consumed = 0, selections_consumed = 0;
    for (std::size_t b = 0; b < index_.order.size(); ++b) {
      const auto& range = Block(b);
      for (auto j = Start(b); j < Count(b); ++j) {
        budget_.Step(); const OpId id{range.first_op.value + j}; const auto& op = graph_.ops[id.value];
        witness_ = EvaluationAt(graph_, plan_, id, EvaluationObligation::kLogicalInterval);
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
          const bool base = field && selections_.fields[id.value] == *producer;
          const bool literal = Object(*producer) && selections_.object_consumers[producer->value] == use;
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
          if (literal) ++objects_consumed;
          if (logical) ++selections_consumed;
        }
        if (field) { intervals_[selections_.fields[id.value]->value] = intervals_[id.value]; ++definition; }
        serial += count;
      }
    }
    // Route registration and operand movement commit together. Discovering a
    // consumer does not prove that this domain owns its inputs (a literal may
    // already own them). Each unique selection occurrence must actually pass
    // the reverse interval proof before any member is published. The forward
    // verifier independently accounts for those same occurrences.
    return selections_consumed == route_.size() &&
        (!object_ || (objects_consumed == object_->allocations.size() && definition == object_->definitions.size()));
  }
  void Verify(std::vector<std::uint8_t>& consumed) {
    std::size_t serial = 0, definition = 0;
    for (std::size_t b = 0; b < index_.order.size(); ++b) {
      const auto& range = Block(b);
      for (auto j = Start(b); j < Count(b); ++j) {
        budget_.Step(); const OpId id{range.first_op.value + j}; const auto& op = graph_.ops[id.value];
        witness_ = EvaluationAt(graph_, plan_, id, EvaluationObligation::kLogicalInterval);
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
          const bool base = field && selections_.fields[id.value] == *producer;
          witness_ = EvaluationAt(graph_, plan_, *producer, EvaluationObligation::kOperandOwnership);
          witness_.use = witness_.expected_consumer = use; witness_.expected_owner = OperationLocation(graph_, id);
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
          intervals_[selections_.fields[id.value]->value] = intervals_[id.value]; ++definition;
        }
        serial += count;
      }
    }
    Require(!object_ || definition == object_->definitions.size());
  }
 private:
  EvaluationEvidence witness_;
  void Require(bool ok) const { if (!ok) throw EvaluationFailure{EvaluationError::kOwnership, witness_}; }
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
      for (const auto allocation : object_->allocations) { budget_.Step(); Needed(allocation); }
      for (const auto field : object_->definitions) { budget_.Step(); Needed(field); }
    }
    auto definition = object_ ? object_->definitions.size() : 0;
    // Reverse the contracted region order once. Only the final consumers and
    // the trees they own need the additional composition proof; all intervening
    // effects still advance the forward event clock in Build and Verify.
    for (auto b = index_.order.size(); b != 0; --b) {
      const auto& range = Block(b - 1);
      for (auto j = Count(b - 1); j != Start(b - 1); --j) {
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
          const bool object = Object(*producer) && ((field && selections_.fields[id.value] == *producer) ||
              selections_.object_consumers[producer->value] == use);
          if (owned || object || index_.branches[producer->value] == use || Candidate(id, occurrence, *producer, field)) Needed(*producer);
        }
      }
    }
  }
  const WorkBlock& Block(std::size_t index) const {
    return graph_.blocks[index_.order[index].value];
  }
  std::uint32_t Start(std::size_t block) const { return index_.starts[index_.order[block].value]; }
  std::uint32_t Count(std::size_t block) const {
    // The transformation ends at its final consumer. Later join operations keep
    // their separately verified local schedule, including other literal groups.
    return block + 1 < index_.order.size() ? Block(block).op_count : graph_.ops[last_owner_.value].order + 1;
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
    const auto operand = ResolveEvaluationOperand(plan_, use.operand);
    const auto* value = std::get_if<ValueId>(&operand); if (!value) return {};
    if (index_.values[value->value]) return index_.values[value->value];
    const auto* result = std::get_if<OpResult>(&graph_.values[value->value].origin);
    return result ? std::optional{result->op} : std::nullopt;
  }
  bool Inside(OpId id) const {
    const auto& op = graph_.ops[id.value];
    return index_.blocks[op.block.value] && op.order >= index_.starts[op.block.value] &&
        (op.block != graph_.ops[last_owner_.value].block || op.order <= graph_.ops[last_owner_.value].order);
  }
  bool Object(OpId id) const {
    return object_ && selections_.object_roots[id.value] == object_->allocation;
  }
  const ValueProgram& graph_; const EvaluationPlan& plan_;
  const SelectionIndex& selections_;
  std::span<const SelectionRegion* const> route_; std::vector<Interval>& intervals_;
  DomainIndex& index_; TreeCheckBudget& budget_; std::optional<SelectedObject> object_; OpId last_owner_;
  // Inline ancestors of the selection consumer, outermost last (see ctor).
  std::vector<OpId> chain_;
};
// The one-consumer forest is a proposal, not movement permission. The
// Domain's reverse construction and independent forward verification must both
// cover its original event interval. Every value/operand is indexed once.
struct ReferenceRouteIndex {
  std::vector<std::optional<OpId>> roots;
  std::vector<std::size_t> owner_starts, region_starts;
  std::vector<OpId> owners;
  std::vector<std::size_t> members;
  ReferenceRouteIndex(const ValueProgram& graph, const EvaluationPlan& plan,
      std::span<const SelectionRegion> regions, const SelectionIndex& index, TreeCheckBudget& budget, bool proposed = false) {
    budget.Array(roots, graph.ops.size());
    budget.Array(owner_starts, graph.ops.size()+1); budget.Array(region_starts, graph.ops.size()+1);
    std::vector<std::uint8_t> allocations; budget.Array(allocations, graph.ops.size());
    if (plan.literals) for (const auto& object: plan.literals->objects) {
      budget.Step(); allocations[object.allocation.value] = 1;
    }
    const auto module_result = IndexModuleEvaluations(graph, plan.modules, budget.Remaining());
    const auto* modules = std::get_if<ModuleEvaluationIndex>(&module_result);
    if (!modules) throw std::get<EvaluationFailure>(module_result);
    budget.Admit(modules->allocated_bytes, 1); budget.Step(modules->work);
    const auto uses = IndexEvaluationUses(graph, plan, *modules, allocations, plan.class_projection.owners,
        plan.cells ? &*plan.cells : nullptr, plan.receiver_guards, budget);
    std::vector<std::optional<OpId>> parents;
    enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
    std::vector<Visit> state;
    std::vector<OpId> path;
    budget.Array(parents, graph.ops.size()); budget.Array(state, graph.ops.size());
    budget.Array(path, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const OpId id{static_cast<std::uint32_t>(i)};
      if (const auto property = MemberProperty(graph, plan, id)) {
        const auto original = BeforeLogicalFusion(graph, plan, property->value);
        if (original.kind == EvaluationKind::kRoot && original.temporary == TemporaryReason::kCrossRegionValue)
          roots[i] = id;
      }
      const auto original = BeforeLogicalFusion(graph, plan, i);
      const auto use = uses[i].first;
      // A saved member invocation may itself be an inline argument of a
      // surrounding saved member invocation. Its result has one real use;
      // keep following that eager ancestry so ONE interval checks both
      // references, rather than committing only the innermost callee.
      if (!use || uses[i].multiple ||
          (original.kind != EvaluationKind::kInline &&
           !(original.kind == EvaluationKind::kRoot && original.temporary == TemporaryReason::kCrossRegionValue))) continue;
      const auto& occurrence = graph.uses[use->value];
      const auto* owner = std::get_if<OpId>(&occurrence.owner);
      const auto shape = owner ? DescribeEvaluation(graph.ops[owner->value].operation) : std::nullopt;
      if (!owner || !shape || shape->NeedsSnapshot(occurrence.role) ||
          occurrence.role == OperandRole::kReceiver || PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand)) continue;
      parents[i] = *owner;
    }
    if(proposed) for(const auto& region:regions) {
      budget.Step();const auto use=index.consumers[region.branch.value].InlineUse();
      if(!region.parent && region.complete_phi && use)
        parents[region.branch.value]=std::get<OpId>(graph.uses[use->value].owner);
    }
    // Compress the actual one-consumer forest. Definition/consumer OpId order
    // is not an availability proof or an admission prerequisite.
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); if (state[i] == Visit::kDone) continue;
      OpId cursor{static_cast<std::uint32_t>(i)}; std::size_t count = 0;
      while (state[cursor.value] == Visit::kUnseen) {
        budget.Step(); state[cursor.value] = Visit::kActive; path[count++] = cursor;
        if (!parents[cursor.value]) break;
        cursor = *parents[cursor.value];
      }
      Require(!parents[cursor.value] || state[cursor.value] == Visit::kDone);
      auto root = roots[cursor.value];
      while (count) {
        budget.Step(); const auto id = path[--count];
        if (!root) root = roots[id.value];
        roots[id.value] = root; state[id.value] = Visit::kDone;
      }
    }
    for (const auto root : roots) { budget.Step(); if (root) ++owner_starts[root->value+1]; }
    const auto order = SelectionOrder(graph, regions, budget);
    for (const auto r: order) {
      budget.Step(); const auto& region = regions[r];
      const auto use = index.consumers[region.branch.value].InlineUse();
      if (region.parent || !region.complete_phi || !use || (!proposed && (plan.logical_values.empty() ||
          plan.logical_values[region.merged.value] != region.branch))) continue;
      const auto owner = std::get<OpId>(graph.uses[use->value].owner);
      if (roots[owner.value]) ++region_starts[roots[owner.value]->value+1];
    }
    for (std::size_t i=1;i<owner_starts.size();++i) {
      budget.Step(); owner_starts[i]+=owner_starts[i-1]; region_starts[i]+=region_starts[i-1];
    }
    budget.Array(owners,owner_starts.back()); budget.Array(members,region_starts.back());
    std::vector<std::size_t> next_owner,next_region;
    budget.Array(next_owner,owner_starts.size()); budget.Array(next_region,region_starts.size());
    budget.Step(owner_starts.size()+region_starts.size());
    std::copy(owner_starts.begin(),owner_starts.end(),next_owner.begin());
    std::copy(region_starts.begin(),region_starts.end(),next_region.begin());
    for(std::size_t i=0;i<roots.size();++i) { budget.Step(); if(roots[i]) owners[next_owner[roots[i]->value]++]={static_cast<std::uint32_t>(i)}; }
    for(const auto r:order) {
      budget.Step(); const auto& region=regions[r]; const auto use=index.consumers[region.branch.value].InlineUse();
      if(region.parent || !region.complete_phi || !use || (!proposed && (plan.logical_values.empty() || plan.logical_values[region.merged.value]!=region.branch))) continue;
      const auto owner=std::get<OpId>(graph.uses[use->value].owner);
      if(roots[owner.value]) members[next_region[roots[owner.value]->value]++]=r;
    }
  }
  std::span<const OpId> Owners(OpId call) const { return std::span{owners}.subspan(owner_starts[call.value],owner_starts[call.value+1]-owner_starts[call.value]); }
  std::span<const std::size_t> Regions(OpId call) const { return std::span{members}.subspan(region_starts[call.value],region_starts[call.value+1]-region_starts[call.value]); }
};
bool ReferenceRoute(const ValueProgram& graph, std::span<const SelectionRegion> regions,
    const ReferenceRouteIndex& references, const DomainIndex& index, OpId call,
    std::vector<const SelectionRegion*>& route, TreeCheckBudget& budget) {
  route.clear(); const auto members = references.Regions(call);
  if (members.empty()) return false;
  const auto linear = [&](WorkBlockId from, WorkBlockId to) {
    return from == to || (index.corridor[from.value] &&
        index.corridor[from.value] == index.corridor[to.value] &&
        index.position[from.value] < index.position[to.value]);
  };
  for (const auto r : members) {
    budget.Step(); const auto& region = regions[r];
    if (!route.empty() && !linear(route.back()->join, region.header)) return false;
    route.push_back(&region);
  }
  return linear(route.back()->join, graph.ops[call.value].block);
}
void BuildJointReferences(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  bool unmet=false;
  for(std::size_t i=0;i<graph.ops.size();++i) {
    budget.Step();const OpId call{static_cast<std::uint32_t>(i)};
    if(const auto property=MemberProperty(graph,plan,call);property && !MemberReferenceInline(graph,plan,call,*property))unmet=true;
  }
  if(!unmet)return;
  const auto proposals=FindJointSelectionUses(graph,plan,budget);if(proposals.empty())return;
  const auto regions=Regions(graph,plan,budget,proposals);
  const auto index=IndexSelections(graph,plan,regions,budget,proposals);
  DomainIndex domain_index(graph,budget);
  const ReferenceRouteIndex references(graph,plan,regions,index,budget,true);
  std::vector<Interval> intervals;std::vector<SelectionBlock> used;
  std::vector<const SelectionRegion*> route;std::vector<std::pair<OpId,UseId>> selected;
  budget.Array(intervals,graph.ops.size());budget.Array(used,graph.blocks.size());
  budget.Array(route,regions.size());route.clear();budget.Array(selected,graph.ops.size());selected.clear();
  const auto route_capacity=plan.logical_reference_routes.size()+proposals.size();
  budget.Admit(route_capacity,sizeof(LogicalReferenceRoute));plan.logical_reference_routes.reserve(route_capacity);
  budget.Admit(plan.logical_reference_routes.capacity()-route_capacity,sizeof(LogicalReferenceRoute));
  std::vector<std::uint8_t> committed,owned_branches;budget.Array(committed,graph.values.size());budget.Array(owned_branches,graph.ops.size());
  for(std::size_t i=0;i<graph.ops.size();++i) {
    budget.Step();const OpId call{static_cast<std::uint32_t>(i)};
    const auto property=MemberProperty(graph,plan,call);
    if(!property || MemberReferenceInline(graph,plan,call,*property) || references.roots[i]!=call ||
        !ReferenceRoute(graph,regions,references,domain_index,call,route,budget))continue;
    selected.clear();
    if(!Domain(graph,plan,index,route,intervals,domain_index,used,budget,{},references.Owners(call),call).Build(selected))continue;
    if(std::none_of(selected.begin(),selected.end(),[&](const auto& item){return item.first==*property && item.second==graph.ops[i].first_use;}))continue;
    if(plan.logical_values.empty())budget.Array(plan.logical_values,graph.values.size());
    LogicalReferenceRoute proof{call,{}};budget.Array(proof.branches,route.size());
    for(std::size_t j=0;j<route.size();++j) {
      budget.Step();const auto& region=*route[j];proof.branches[j]=region.branch;committed[region.merged.value]=1;owned_branches[region.branch.value]=1;
      plan.logical_values[region.merged.value]=region.branch;
      plan.operations[region.branch.value]={EvaluationKind::kLogicalValue,index.consumers[region.branch.value].InlineUse(),{}};
    }
    plan.logical_reference_routes.push_back(std::move(proof));
    for(const auto& [producer,use]:selected){budget.Step();plan.operations[producer.value]={EvaluationKind::kInline,use,{}};}
  }
  // Discovery stores descendants before their owner. Commit each original
  // forwarding edge with the containing reference route, never as a new phi
  // alias or as a verifier-error retry.
  for(std::size_t i=regions.size();i-- > 0;) {
    budget.Step();const auto& region=regions[i];
    if(!region.parent || !owned_branches[region.parent->value])continue;
    owned_branches[region.branch.value]=1;
    if(region.forwarded) {
      plan.logical_values[region.merged.value]=region.branch;
      plan.operations[region.branch.value]={EvaluationKind::kLogicalValue,region.forwarded,{}};
    }
  }
  budget.Admit(proposals.size(),sizeof(JointSelectionUse));plan.joint_selection_uses.reserve(proposals.size());
  budget.Admit(plan.joint_selection_uses.capacity()-proposals.size(),sizeof(JointSelectionUse));
  for(const auto& proof:proposals) {
    budget.Step();if(!committed[proof.value.value])continue;
    budget.Admit(proof.predicates.size(),sizeof(UseId));plan.joint_selection_uses.push_back(proof);
    budget.Admit(plan.joint_selection_uses.back().predicates.capacity()-proof.predicates.size(),sizeof(UseId));
  }
  budget.Step(plan.logical_reference_routes.size() * std::bit_width(plan.logical_reference_routes.size()));
  std::sort(plan.logical_reference_routes.begin(),plan.logical_reference_routes.end(),[](const auto& a,const auto& b){return a.call<b.call;});
}
}  // namespace
const PhaseSelectionOperand* FindPhaseSelectionOperand(const ValueProgram& graph, const EvaluationPlan& plan, OpId branch) {
  if (plan.flow != EvaluationFlow::kPhased || branch.value >= graph.ops.size()) return nullptr;
  const auto found = std::lower_bound(plan.phase_selections.begin(), plan.phase_selections.end(), branch,
      [](const auto& entry, OpId key) { return entry.branch < key; });
  return found != plan.phase_selections.end() && found->branch == branch ? &*found : nullptr;
}

bool CrossLogicalInline(const ValueProgram& graph, const EvaluationPlan& plan, OpId producer) {
  const auto& placement = plan.operations[producer.value];
  if (placement.kind != EvaluationKind::kInline || !placement.consumer ||
      placement.consumer->value >= graph.uses.size()) return false;
  if (producer.value < plan.closed_expressions.size() && plan.closed_expressions[producer.value]) return false;
  if (plan.flow == EvaluationFlow::kPhased) {
    return (producer.value < plan.phase_transports.size() && plan.phase_transports[producer.value]) ||
        (producer.value < plan.phase_references.size() && plan.phase_references[producer.value]);
  }
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
  const auto regions = Regions(graph, plan, budget); if (regions.empty()) return;
  const auto index = IndexSelections(graph, plan, regions, budget); DomainIndex domain_index(graph, budget);
  std::vector<Interval> intervals; std::vector<SelectionBlock> used;
  std::vector<std::uint8_t> attempted_chains;
  std::vector<std::pair<OpId, UseId>> selected; std::vector<const SelectionRegion*> route;
  budget.Array(intervals, graph.ops.size()); budget.Array(used, graph.blocks.size());
  budget.Array(attempted_chains, graph.ops.size());
  budget.Admit(graph.ops.size(), sizeof(std::pair<OpId, UseId>)); selected.reserve(graph.ops.size());
  budget.Admit(selected.capacity() - graph.ops.size(), sizeof(std::pair<OpId, UseId>));
  budget.Admit(regions.size(), sizeof(const SelectionRegion*)); route.reserve(regions.size());
  budget.Admit(route.capacity() - regions.size(), sizeof(const SelectionRegion*));
  for (const auto r : SelectionOrder(graph, regions, budget)) {
    budget.Step(); const auto& region = regions[r];
    if (!index.consumers[region.branch.value].InlineUse()) continue;
    if (!Available(graph, region, used)) continue;
    selected.clear(); route.clear(); route.push_back(&region);
    auto object = ObjectRoute(graph, plan, regions, index, used, route, budget);
    if (object && !Domain(graph, plan, index, route, intervals, domain_index, used, budget, object).Build(selected)) object.reset();
    if (!object) {
      selected.clear(); route.resize(1);
      if (!attempted_chains[region.branch.value]) {
        attempted_chains[region.branch.value] = 1;
        while (const auto next = index.continuations[route.back()->branch.value]) {
          budget.Step(); const auto& following = regions[*next];
          if (!index.consumers[following.branch.value].InlineUse() ||
              attempted_chains[following.branch.value] || !Available(graph, following, used)) break;
          attempted_chains[following.branch.value] = 1; route.push_back(&following);
        }
      }
      if (!Domain(graph, plan, index, route, intervals, domain_index, used, budget).Build(selected)) {
        if (route.size() == 1) continue;
        selected.clear(); route.resize(1);
        if (!Domain(graph, plan, index, route, intervals, domain_index, used, budget).Build(selected)) continue;
      }
    }
    if (object) {
      if (plan.logical_objects.empty()) budget.Array(plan.logical_objects, graph.ops.size());
      for (const auto allocation : object->allocations) {
        budget.Step(); const auto& literal = plan.literals->objects[*index.objects[allocation.value]];
        plan.logical_objects[allocation.value] = plan.literals->definitions[literal.first + literal.count - 1];
        plan.operations[allocation.value] = {EvaluationKind::kInline, index.object_consumers[allocation.value], {}};
      }
      for (const auto field : object->definitions) { budget.Step(); plan.operations[field.value] = {EvaluationKind::kInitializer, {}, {}}; }
    }
    if (plan.logical_values.empty()) budget.Array(plan.logical_values, graph.values.size());
    for (const auto* selected_region : route) {
      budget.Step(); plan.logical_values[selected_region->merged.value] = selected_region->branch;
      plan.operations[selected_region->branch.value] = {EvaluationKind::kLogicalValue, index.consumers[selected_region->branch.value].InlineUse(), {}};
      used[selected_region->header.value].header = true;
      const auto consumer = selected_region == route.back() && object ? object->consumer :
          *index.consumers[selected_region->branch.value].InlineUse();
      // The consumed join prefix ends at the domain's final consumer.
      used[selected_region->join.value].prefix_end = selected_region == route.back() && !object
          ? InlineAncestors(graph, plan, std::get<OpId>(graph.uses[consumer.value].owner), budget)
          : std::get<OpId>(graph.uses[consumer.value].owner);
    }
    for (const auto& [producer, use] : selected) { budget.Step(); plan.operations[producer.value] = {EvaluationKind::kInline, use, {}}; }
  }
  for (const auto& region : regions) {
    budget.Step(); if (!region.forwarded) continue;
    if (plan.logical_values.empty()) budget.Array(plan.logical_values, graph.values.size());
    plan.logical_values[region.merged.value] = region.branch;
    plan.operations[region.branch.value] = {EvaluationKind::kLogicalValue, region.forwarded, {}};
  }
  std::size_t unmet = 0;
  for (std::size_t i=0;i<graph.ops.size();++i) {
    budget.Step(); const OpId call{static_cast<std::uint32_t>(i)};
    if (const auto property=MemberProperty(graph,plan,call); property && !MemberReferenceInline(graph,plan,call,*property)) ++unmet;
  }
  if (!unmet) return; // Existing successful plans retain every old decision.
  const ReferenceRouteIndex references(graph,plan,regions,index,budget);
  std::vector<SelectionBlock> empty_used; budget.Array(empty_used,graph.blocks.size());
  for (std::size_t i=0;i<graph.ops.size();++i) {
    budget.Step(); const OpId call{static_cast<std::uint32_t>(i)};
    const auto property=MemberProperty(graph,plan,call);
    if(!property || MemberReferenceInline(graph,plan,call,*property) || references.roots[i]!=call ||
        !ReferenceRoute(graph,regions,references,domain_index,call,route,budget)) continue;
    selected.clear();
    if(!Domain(graph,plan,index,route,intervals,domain_index,empty_used,budget,{},references.Owners(call),call).Build(selected)) continue;
    if(std::none_of(selected.begin(),selected.end(),[&](const auto& item){return item.first==*property && item.second==graph.ops[i].first_use;})) continue;
    LogicalReferenceRoute proof{call,{}};
    budget.Admit(route.size(),sizeof(OpId)); proof.branches.reserve(route.size());
    budget.Admit(proof.branches.capacity()-route.size(),sizeof(OpId));
    for(const auto* region:route) {budget.Step();proof.branches.push_back(region->branch);}
    if (plan.logical_reference_routes.empty()) {
      budget.Admit(unmet,sizeof(LogicalReferenceRoute)); plan.logical_reference_routes.reserve(unmet);
      budget.Admit(plan.logical_reference_routes.capacity()-unmet,sizeof(LogicalReferenceRoute));
    }
    plan.logical_reference_routes.push_back(std::move(proof));
    for(const auto& [producer,use]:selected) {budget.Step();plan.operations[producer.value]={EvaluationKind::kInline,use,{}};}
  }
  BuildJointReferences(graph,plan,budget);
}
void VerifyLogicalValues(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  EvaluationEvidence witness; witness.obligation = EvaluationObligation::kLogicalInput;
  const auto Require = [&](bool ok) { if (!ok) throw EvaluationFailure{EvaluationError::kOwnership, witness}; };
  VerifyPredicateSnapshots(graph, plan, budget);
  if (plan.flow == EvaluationFlow::kPhased) {
    Require(plan.logical_reference_routes.empty() && plan.joint_selection_uses.empty());
    VerifyPhaseSelectionOperands(graph, plan, budget);
    VerifyPhasedObjectOperands(graph, plan, budget); return;
  }
  Require(plan.phase_selections.empty() && plan.phase_transports.empty() && plan.phase_prefixes.empty() && plan.phase_references.empty() && plan.phase_children.empty() && plan.phase_compositions.empty());
  Require(plan.logical_objects.empty() || plan.logical_objects.size() == graph.ops.size());
  Require(plan.logical_values.empty() || (plan.flow == EvaluationFlow::kNormal && plan.logical_values.size() == graph.values.size()));
  if (plan.logical_values.empty()) {
    Require(plan.logical_objects.empty() && plan.logical_reference_routes.empty() && plan.joint_selection_uses.empty());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(i)}, EvaluationObligation::kLogicalCoverage); Require(plan.operations[i].kind != EvaluationKind::kLogicalValue &&
          !(plan.operations[i].kind == EvaluationKind::kInitializer &&
            IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, OpId{static_cast<std::uint32_t>(i)})) &&
          !FieldValue(graph, plan, OpId{static_cast<std::uint32_t>(i)}) &&
          !CrossLogicalInline(graph, plan, OpId{static_cast<std::uint32_t>(i)}));
    }
    return;
  }
  // Validate the untrusted certificate census before any region view indexes
  // its value/keep identities. Range/owner mutations must be typed refusals.
  VerifyJointSelectionUses(graph, plan, plan.joint_selection_uses, budget);
  const auto regions = Regions(graph, plan, budget); const auto index = IndexSelections(graph, plan, regions, budget, plan.joint_selection_uses);
  DomainIndex domain_index(graph, budget);
  std::vector<Interval> intervals; std::vector<SelectionBlock> used;
  std::vector<std::uint8_t> consumed, values, objects;
  std::vector<const SelectionRegion*> route;
  budget.Array(intervals, graph.ops.size()); budget.Array(used, graph.blocks.size());
  budget.Array(consumed, graph.ops.size()); budget.Array(values, graph.values.size()); budget.Array(objects, graph.ops.size());
  budget.Admit(regions.size(), sizeof(const SelectionRegion*)); route.reserve(regions.size());
  budget.Admit(route.capacity() - regions.size(), sizeof(const SelectionRegion*));
  std::vector<std::uint8_t> reference_branches; budget.Array(reference_branches,graph.ops.size());
  if(!plan.logical_reference_routes.empty()) {
    const ReferenceRouteIndex references(graph,plan,regions,index,budget,!plan.joint_selection_uses.empty());
    std::optional<OpId> previous;
    for(const auto& proof:plan.logical_reference_routes) {
      budget.Step(); Require(proof.call.value<graph.ops.size());
      witness=EvaluationAt(graph,plan,proof.call,EvaluationObligation::kLogicalJoin);
      Require(!previous || *previous<proof.call); previous=proof.call;
      const auto property=MemberProperty(graph,plan,proof.call);
      Require(property && references.roots[proof.call.value]==proof.call && MemberReferenceInline(graph,plan,proof.call,*property));
      Require(ReferenceRoute(graph,regions,references,domain_index,proof.call,route,budget) && route.size()==proof.branches.size());
      for(std::size_t j=0;j<route.size();++j) {
        budget.Step();const auto& region=*route[j];
        Require(proof.branches[j]==region.branch && !reference_branches[region.branch.value] && !values[region.merged.value]);
        reference_branches[region.branch.value]=1;values[region.merged.value]=1;
        const auto& placement=plan.operations[region.branch.value];
        Require(placement.kind==EvaluationKind::kLogicalValue && placement.consumer==index.consumers[region.branch.value].InlineUse() && !placement.temporary);
      }
      Domain(graph,plan,index,route,intervals,domain_index,used,budget,{},references.Owners(proof.call),proof.call).Verify(consumed);
    }
  }
  for(const auto& proof:plan.joint_selection_uses) {
    budget.Step();Require(proof.value.value<plan.logical_values.size() && plan.logical_values[proof.value.value].has_value());
    const auto branch=*plan.logical_values[proof.value.value];
    Require(reference_branches[branch.value] && index.consumers[branch.value].keep==proof.keep);
    const auto route=std::lower_bound(plan.logical_reference_routes.begin(),plan.logical_reference_routes.end(),proof.call,
        [](const auto& entry,auto call){return entry.call<call;});
    Require(route!=plan.logical_reference_routes.end() && route->call==proof.call);
  }
  for (const auto r : SelectionOrder(graph, regions, budget)) {
    budget.Step(); const auto& region = regions[r];
    if(reference_branches[region.branch.value]) continue;
    witness = EvaluationAt(graph, plan, region.branch, EvaluationObligation::kLogicalInput);
    if (!index.consumers[region.branch.value].InlineUse()) {
      // Child/partial selections have their own phi or parent proof below;
      // they are not entries in this top-level continuation index.
      if (region.parent || !region.complete_phi || region.forwarded) continue;
      const auto& exit = index.consumers[region.branch.value];
      witness.selection_exit = exit.reason; witness.selection_edge = exit.edge;
      witness.use = exit.witness; witness.expected_consumer = exit.use;
      if (exit.witness) if (const auto* owner = std::get_if<OpId>(&graph.uses[exit.witness->value].owner))
        witness.actual_owner = EvaluationAt(graph, plan, *owner, EvaluationObligation::kLogicalInput).location;
      Require(!plan.logical_values[region.merged.value]);
      continue;
    }
    if (!plan.logical_values[region.merged.value] || values[region.merged.value]) continue;
    Require(Available(graph, region, used) && index.consumers[region.branch.value].InlineUse());
    route.clear(); route.push_back(&region);
    auto object = ObjectRoute(graph, plan, regions, index, used, route, budget);
    if (object && (plan.logical_objects.empty() || !plan.logical_objects[object->allocation.value])) object.reset();
    if (!object) {
      route.resize(1);
      while (const auto next = index.continuations[route.back()->branch.value]) {
        budget.Step(); const auto& following = regions[*next];
        if (!index.consumers[following.branch.value].InlineUse() ||
            plan.logical_values[following.merged.value] != following.branch || values[following.merged.value]) break;
        Require(Available(graph, following, used)); route.push_back(&following);
      }
    }
    if (object) {
      for (const auto allocation : object->allocations) {
        budget.Step(); objects[allocation.value] = 1;
        const auto& literal = plan.literals->objects[*index.objects[allocation.value]];
        Require(plan.operations[allocation.value].kind == EvaluationKind::kInline &&
            plan.operations[allocation.value].consumer == index.object_consumers[allocation.value] &&
            !plan.operations[allocation.value].temporary &&
            plan.logical_objects[allocation.value] == plan.literals->definitions[literal.first + literal.count - 1]);
      }
      for (const auto field : object->definitions) {
        budget.Step(); Require(plan.operations[field.value].kind == EvaluationKind::kInitializer &&
            !plan.operations[field.value].consumer && !plan.operations[field.value].temporary);
      }
    }
    for (const auto* selected_region : route) {
      budget.Step(); const auto& placement = plan.operations[selected_region->branch.value];
      witness = EvaluationAt(graph, plan, selected_region->branch, EvaluationObligation::kLogicalCoverage);
      witness.value = selected_region->merged;
      witness.expected_consumer = index.consumers[selected_region->branch.value].InlineUse();
      witness.logical_coverage = LogicalCoverageEvidence{LogicalCoverageCheck::kRouteMember,
          selected_region->branch, route.front()->branch, route.back()->branch,
          plan.logical_values[selected_region->merged.value].has_value(), values[selected_region->merged.value] != 0};
      Require(plan.logical_values[selected_region->merged.value] == selected_region->branch &&
          placement.kind == EvaluationKind::kLogicalValue && placement.consumer == index.consumers[selected_region->branch.value].InlineUse() && !placement.temporary);
      values[selected_region->merged.value] = 1;
    }
    Domain(graph, plan, index, route, intervals, domain_index, used, budget, object).Verify(consumed);
    for (const auto* selected_region : route) {
      budget.Step(); used[selected_region->header.value].header = true;
      const auto consumer = selected_region == route.back() && object ? object->consumer :
          *index.consumers[selected_region->branch.value].InlineUse();
      used[selected_region->join.value].prefix_end = std::get<OpId>(graph.uses[consumer.value].owner);
    }
  }
  for (const auto& region : regions) {
    budget.Step(); if (!region.forwarded) continue;
    const auto& placement = plan.operations[region.branch.value];
    witness = EvaluationAt(graph, plan, region.branch, EvaluationObligation::kLogicalCoverage);
    witness.value = region.merged; witness.expected_consumer = region.forwarded;
    witness.logical_coverage = LogicalCoverageEvidence{LogicalCoverageCheck::kForwardedRegistration,
        region.branch, {}, {}, plan.logical_values[region.merged.value].has_value(), values[region.merged.value] != 0};
    Require(region.parent && region.complete_phi && !values[region.merged.value] && !consumed[region.branch.value] &&
        plan.logical_values[region.merged.value] == region.branch && placement.kind == EvaluationKind::kLogicalValue &&
        placement.consumer == region.forwarded && !placement.temporary);
    values[region.merged.value] = 1; consumed[region.branch.value] = 1;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    budget.Step(); witness = {}; witness.obligation = EvaluationObligation::kLogicalCoverage;
    if (plan.logical_values[i]) witness = EvaluationAt(graph, plan, *plan.logical_values[i], EvaluationObligation::kLogicalCoverage);
    witness.value = ValueId{static_cast<std::uint32_t>(i)};
    witness.logical_coverage = LogicalCoverageEvidence{LogicalCoverageCheck::kValueMap,
        plan.logical_values[i], {}, {}, plan.logical_values[i].has_value(), values[i] != 0};
    Require(plan.logical_values[i].has_value() == (values[i] != 0));
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step();
    witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(i)}, EvaluationObligation::kLogicalCoverage);
    witness.logical_coverage = LogicalCoverageEvidence{LogicalCoverageCheck::kConsumedEvent,
        {}, {}, {}, true, consumed[i] != 0};
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
