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
      kind == EvaluationKind::kLogicalLeft || kind == EvaluationKind::kLogicalValue || kind == EvaluationKind::kPhiInput;
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
  const auto shape = DescribeEvaluation(op.operation);
  // The corridor owns a pure control transfer as an anchor, not a language
  // effect. Keeping it on the effect clock would make block splitting change
  // expression ownership. Conditional transfers remain atomic events above.
  if (op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible && shape &&
      shape->control == ControlEvaluation::kNormal && shape->Count() == 0) return 0;
  return OrderedEvaluation(op) || IsEnvironmentState(op.operation);
}
std::optional<UseId> Consumer(const ValueProgram& graph, const SelectionRegion& region, TreeCheckBudget& budget,
    const SelectionRegion* logical_left = nullptr) {
  std::optional<UseId> found;
  bool keep = false;
  for (auto use = graph.values[region.merged.value].first_use; use; use = graph.uses[use->value].next) {
    budget.Step(); const auto& occurrence = graph.uses[use->value];
    if (occurrence.life != ValueLife::kLive) continue;
    if (logical_left && occurrence.owner == decltype(occurrence.owner){logical_left->merged}) {
      const auto* edge = occurrence.incoming ? std::get_if<EdgeId>(&*occurrence.incoming) : nullptr;
      if (keep || !edge || logical_left->keep_input != *edge || occurrence.role != OperandRole::kPhiInput) return {};
      keep = true; continue;
    }
    const auto* owner = std::get_if<OpId>(&occurrence.owner);
    if (found || !owner || graph.ops[owner->value].block != region.join) return {};
    if (logical_left && *use != graph.ops[logical_left->boolean.value].first_use) return {};
    const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
    if (!shape || shape->NeedsSnapshot(occurrence.role) || occurrence.role == OperandRole::kReceiver ||
        PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand)) return {};
    found = use;
  }
  if (logical_left && !keep) return {};
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
struct ObjectRange {
  std::size_t first_object = 0, object_count = 0, first_field = 0, field_count = 0;
};
struct SelectionIndex {
  std::vector<std::optional<std::size_t>> headers, objects;
  std::vector<std::optional<UseId>> consumers;
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
std::optional<UseId> ObjectConsumer(const ValueProgram& graph, const ObjectInit& object,
    const LiteralPlan& literals, const SelectionIndex& index, TreeCheckBudget& budget) {
  if (!object.count) return {};
  const auto last = literals.definitions[object.first + object.count - 1];
  std::optional<UseId> external;
  for (auto at = graph.values[graph.ops[object.allocation.value].result->value].first_use;
       at; at = graph.uses[at->value].next) {
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
    const auto use = ObjectConsumer(graph, object, *plan.literals, index, budget);
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
    std::span<const SelectionRegion> regions, TreeCheckBudget& budget) {
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
  for (const auto& region : regions) {
    budget.Step(); if (region.parent || !region.complete_phi) continue;
    const auto next = index.headers[region.join.value];
    const auto* logical_left = next && !regions[*next].consequent && regions[*next].left == region.merged ? &regions[*next] : nullptr;
    index.consumers[region.branch.value] = Consumer(graph, region, budget, logical_left);
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
    budget.Step(); const auto use = index.consumers[region.branch.value];
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
  const auto first_use = index.consumers[route.front()->branch.value];
  if (!first_use || !plan.literals) return {};
  if (!index.selection_fields[route.front()->branch.value]) return {};
  const auto first = *index.selection_fields[route.front()->branch.value];
  if (!index.fields[first.value]) return {};
  const auto allocation = *index.object_roots[index.fields[first.value]->value];
  const auto& group = index.groups[allocation.value];
  const auto definitions = std::span(index.group_fields).subspan(group.first_field, group.field_count);
  const auto allocations = std::span(index.group_objects).subspan(group.first_object, group.object_count);
  if (definitions.empty() || index.first_selections[allocation.value] != route.front()->branch ||
      graph.ops[allocation.value].block != route.front()->header) return {};
  const auto* buffer = std::get_if<ValueObjectAllocation>(&graph.ops[allocation.value].operation);
  if (!buffer || !buffer->literal) return {};
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
    if (field.use_count != 2 || !std::holds_alternative<ValueOwnWrite>(field.operation) || field.block != block) return {};
  }
  if (!selected) return {};
  const auto external = index.object_consumers[allocation.value];
  if (!external) return {};
  const auto owner = std::get<OpId>(graph.uses[external->value].owner);
  if (graph.ops[owner.value].block != route.back()->join || owner <= definitions.back()) return {};
  for (const auto id : allocations) {
    budget.Step();
    const auto* object = std::get_if<ValueObjectAllocation>(&graph.ops[id.value].operation);
    if (!object || !object->literal || !index.object_consumers[id.value]) return {};
  }
  return SelectedObject{allocation, allocations, definitions, *external};
}
struct DomainIndex {
  std::vector<std::uint8_t> blocks, owners, needed;
  std::vector<OpId> needed_ops;
  std::vector<WorkBlockId> order;
  std::vector<std::uint32_t> starts;
  std::vector<std::optional<WorkBlockId>> previous;
  std::vector<std::optional<OpId>> values;
  std::vector<std::optional<UseId>> branches;
  DomainIndex(const ValueProgram& graph, TreeCheckBudget& budget) {
    budget.Array(blocks, graph.blocks.size()); budget.Array(owners, graph.ops.size());
    budget.Array(values, graph.values.size()); budget.Array(branches, graph.ops.size());
    budget.Array(needed, graph.ops.size()); budget.Array(needed_ops, graph.ops.size()); needed_ops.clear();
    budget.Array(order, graph.blocks.size()); order.clear();
    budget.Array(starts, graph.blocks.size()); budget.Array(previous, graph.blocks.size());
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
      for (;;) {
        budget.Step(); Require(!visited[block.value]); visited[block.value] = 1;
        if (!next[block.value]) break;
        const auto following = *next[block.value];
        previous[following.value] = block; block = following;
      }
    }
  }
};
// Contract selections with their linear execution prefixes and final join.
// Each selection is one conditional event; its arms remain independently checked. A private
// object interval grows at each own definition and is consumed exactly once.
// Shared header/join blocks are visited once, so fields cannot duplicate effects.
class Domain {
 public:
  Domain(const ValueProgram& graph, const EvaluationPlan& plan, const SelectionIndex& selections,
      std::span<const SelectionRegion* const> route, std::vector<Interval>& intervals,
      DomainIndex& index, const std::vector<SelectionBlock>& used, TreeCheckBudget& budget, std::optional<SelectedObject> object = {})
      : graph_(graph), plan_(plan), selections_(selections), route_(route), intervals_(intervals), index_(index), budget_(budget), object_(object),
        last_owner_(std::get<OpId>(graph.uses[(object ? object->consumer :
            *selections.consumers[route.back()->branch.value]).value].owner)) {
    Require(graph.ops[last_owner_.value].block == route.back()->join);
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
      budget_.Step(); index_.order.push_back(route[r]->header); index_.starts[route[r]->header.value] = 0;
    }
    index_.order.push_back(route.back()->join); index_.starts[route.back()->join.value] = 0;
    // Prepay the nonthrowing cleanup as well as index construction.
    budget_.Step(2 * (index_.order.size() + route.size() + (object_ ? object_->definitions.size() : 0)));
    for (const auto id : index_.order) { Require(!index_.blocks[id.value]); index_.blocks[id.value] = 1; }
    for (const auto* region : route) {
      index_.values[region->merged.value] = region->branch;
      index_.branches[region->branch.value] = selections.consumers[region->branch.value];
      const auto owner = std::get<OpId>(graph.uses[index_.branches[region->branch.value]->value].owner);
      index_.owners[owner.value] = 1;
    }
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
  }
  bool Build(std::vector<std::pair<OpId, UseId>>& selected) {
    std::size_t serial = 0, definition = 0, objects_consumed = 0;
    for (std::size_t b = 0; b < index_.order.size(); ++b) {
      const auto& range = Block(b);
      for (auto j = Start(b); j < Count(b); ++j) {
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
        }
        if (field) { intervals_[selections_.fields[id.value]->value] = intervals_[id.value]; ++definition; }
        serial += count;
      }
    }
    return !object_ || (objects_consumed == object_->allocations.size() && definition == object_->definitions.size());
  }
  void Verify(std::vector<std::uint8_t>& consumed) {
    std::size_t serial = 0, definition = 0;
    for (std::size_t b = 0; b < index_.order.size(); ++b) {
      const auto& range = Block(b);
      for (auto j = Start(b); j < Count(b); ++j) {
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
          const bool base = field && selections_.fields[id.value] == *producer;
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
    const auto* value = std::get_if<ValueId>(&use.operand); if (!value) return {};
    if (index_.values[value->value]) return index_.values[value->value];
    const auto* result = std::get_if<OpResult>(&graph_.values[value->value].origin);
    return result ? std::optional{result->op} : std::nullopt;
  }
  bool Inside(OpId id) const {
    const auto& op = graph_.ops[id.value];
    return index_.blocks[op.block.value] && op.order >= index_.starts[op.block.value] &&
        (op.block != route_.back()->join || op.order <= graph_.ops[last_owner_.value].order);
  }
  bool Object(OpId id) const {
    return object_ && selections_.object_roots[id.value] == object_->allocation;
  }
  const ValueProgram& graph_; const EvaluationPlan& plan_;
  const SelectionIndex& selections_;
  std::span<const SelectionRegion* const> route_; std::vector<Interval>& intervals_;
  DomainIndex& index_; TreeCheckBudget& budget_; std::optional<SelectedObject> object_; OpId last_owner_;
};
}  // namespace
const PhaseSelectionCall* FindPhaseSelectionCall(const ValueProgram& graph, const EvaluationPlan& plan, OpId branch) {
  if (plan.flow != EvaluationFlow::kPhased || plan.phase_calls.empty() || branch.value >= plan.operations.size()) return nullptr;
  auto input = plan.operations[branch.value].consumer;
  if (!input || input->value >= graph.uses.size()) return nullptr;
  auto owner = std::get_if<OpId>(&graph.uses[input->value].owner);
  if (!owner) return nullptr;
  if (owner->value < plan.phase_transports.size() && plan.phase_transports[owner->value]) {
    input = plan.phase_transports[owner->value];
    if (input->value >= graph.uses.size()) return nullptr;
    owner = std::get_if<OpId>(&graph.uses[input->value].owner);
  }
  if (!owner || owner->value >= graph.ops.size()) return nullptr;
  const auto& call = graph.ops[owner->value];
  if (!std::holds_alternative<ValueInvoke>(call.operation)) return nullptr;
  const auto operand = ResolveEvaluationOperand(plan, graph.uses[call.first_use.value].operand);
  const auto* value = std::get_if<ValueId>(&operand);
  const auto* source = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
  if (!source) return nullptr;
  const auto found = std::lower_bound(plan.phase_calls.begin(), plan.phase_calls.end(),
      std::pair{source->op, *input}, [](const auto& entry, const auto& key) {
        return entry.callee != key.first ? entry.callee < key.first : entry.argument < key.second;
      });
  return found != plan.phase_calls.end() && found->callee == source->op && found->branch == branch ? &*found : nullptr;
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
    if (!index.consumers[region.branch.value]) continue;
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
          if (attempted_chains[following.branch.value] || !Available(graph, following, used)) break;
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
  if (plan.flow == EvaluationFlow::kPhased) {
    VerifyPhaseSelectionCalls(graph, plan, budget); return;
  }
  Require(plan.phase_calls.empty() && plan.phase_transports.empty() && plan.phase_prefixes.empty() && plan.phase_references.empty());
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
  for (const auto r : SelectionOrder(graph, regions, budget)) {
    budget.Step(); const auto& region = regions[r];
    if (!index.consumers[region.branch.value]) continue;
    if (!plan.logical_values[region.merged.value] || values[region.merged.value]) continue;
    Require(Available(graph, region, used) && index.consumers[region.branch.value]);
    route.clear(); route.push_back(&region);
    auto object = ObjectRoute(graph, plan, regions, index, used, route, budget);
    if (object && (plan.logical_objects.empty() || !plan.logical_objects[object->allocation.value])) object.reset();
    if (!object) {
      route.resize(1);
      while (const auto next = index.continuations[route.back()->branch.value]) {
        budget.Step(); const auto& following = regions[*next];
        if (plan.logical_values[following.merged.value] != following.branch || values[following.merged.value]) break;
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
      Require(plan.logical_values[selected_region->merged.value] == selected_region->branch &&
          placement.kind == EvaluationKind::kLogicalValue && placement.consumer == index.consumers[selected_region->branch.value] && !placement.temporary);
      values[selected_region->merged.value] = 1;
    }
    Domain(graph, plan, index, route, intervals, domain_index, used, budget, object).Verify(consumed);
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
