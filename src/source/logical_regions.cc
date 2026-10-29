#include "constructor_event_view.h"
#include "literal_arm_view.h"
#include "reference_arguments.h"
#include "logical_regions.h"
#include "evaluation_plan.h"
#include "evaluation_liveness.h"
#include "logical_evaluation.h"
#include "tree_checks.h"
#include "module_evaluation.h"
#include "cell_evaluation.h"
#include "call_plan.h"

#include <array>
#include <algorithm>
#include <iterator>

namespace abcd::source {
namespace {
using namespace analysis;
struct BlockSummary {
  std::optional<OpId> last;
  std::optional<OpId> previous;
  std::optional<ValueId> phi;
  bool multiple_phis = false;
  bool empty = true;
};
struct PhiInputs {
  std::array<UseId, 2> uses{};
  unsigned count = 0;
};
struct Arm {
  std::optional<WorkBlockId> block;
  EdgeId incoming;
  std::optional<ControlId> prefix;
};
std::vector<ControlId> ArmTails(const ControlPlan& control, TreeCheckBudget* budget) {
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<ControlId> tails, path;
  std::vector<Visit> state;
  if (budget) {
    budget->Array(tails, control.nodes.size()); budget->Array(path, control.nodes.size());
    budget->Array(state, control.nodes.size());
  } else { tails.resize(control.nodes.size()); path.resize(control.nodes.size()); state.resize(control.nodes.size()); }
  for (std::size_t i = 0; i < control.nodes.size(); ++i) {
    if (budget) budget->Step(); if (state[i] != Visit::kUnseen) continue;
    auto current = ControlId{static_cast<std::uint32_t>(i)}; std::size_t size = 0;
    while (state[current.value] == Visit::kUnseen) {
      if (budget) budget->Step(); state[current.value] = Visit::kActive; path[size++] = current;
      const auto next = control.nodes[current.value].next;
      if (!next) { tails[current.value] = current; state[current.value] = Visit::kDone; --size; break; }
      if (next->value >= control.nodes.size()) return {};
      current = *next;
    }
    if (state[current.value] != Visit::kDone) return {};
    while (size) {
      if (budget) budget->Step(); const auto id = path[--size];
      tails[id.value] = tails[current.value]; state[id.value] = Visit::kDone;
    }
  }
  return tails;
}
bool SameValue(const ValueOperand& operand, ValueId expected) {
  const auto* value = std::get_if<ValueId>(&operand);
  return value && *value == expected;
}
bool Pure(const ValueOp& op) {
  return op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible;
}
bool OwnedGuard(const ValueProgram& graph, const EvaluationPlan& evaluation,
    const ModuleEvaluationIndex& modules, std::size_t id) {
  // The common initialization proof discharges the guard, not the cell read.
  // Fresh evaluation verification checks both that fact and its anchor; no
  // extra consumer or event is introduced by conditional expression syntax.
  if (evaluation.cells && IsInitializedCellGuard(graph, *evaluation.cells,
      OpId{static_cast<std::uint32_t>(id)})) return true;
  // The checked pair retains its throwing guard inside the same selected
  // subtree. Never ignore arbitrary guards or a separately rooted module read.
  if (modules.peers.empty() || !modules.peers[id] ||
      evaluation.operations[id].kind != EvaluationKind::kModuleGuard ||
      !std::holds_alternative<ValueGuard>(graph.ops[id].operation)) return false;
  const auto read = modules.peers[id]->value;
  const auto kind = evaluation.operations[read].kind;
  return graph.ops[read].block == graph.ops[id].block &&
      (kind == EvaluationKind::kInline || kind == EvaluationKind::kPhiInput);
}
// The ControlPlan owns single-entry region membership. This structural summary
// locates an arm's terminal; expression discovery separately closes its prefix.
std::optional<Arm> ReadArm(const ValueProgram& graph, const ControlPlan& control,
    const std::optional<ControlId>& id, WorkBlockId join, EdgeId entry, std::span<const ControlId> tails) {
  if (!id) {
    if (graph.edges[entry.value].target == join) return Arm{{}, entry};
    return {};
  }
  if (graph.edges[entry.value].target != control.nodes[id->value].block) return {};
  const auto tail = tails[id->value]; const auto& node = control.nodes[tail.value];
  if (node.next || node.arms || !std::holds_alternative<std::monostate>(node.synthetic)) return {};
  const auto& block = graph.blocks[node.block.value];
  if (block.edge_count != 1 ||
      graph.edges[block.first_edge.value].target != join) return {};
  return Arm{node.block, block.first_edge, tail != *id ? id : std::nullopt};
}
}  // namespace
std::vector<SelectionRegion> FindLogicalRegions(const ValueProgram& graph, const ControlPlan& control, TreeCheckBudget* budget) {
  std::vector<SelectionRegion> result;
  if (!graph.phases.empty() || control.view != ControlViewKind::kNormal ||
      graph.revision != control.revision || graph.cfg_revision != control.cfg_revision) return result;
  const auto tails = ArmTails(control, budget); if (tails.empty()) return result;
  std::vector<BlockSummary> blocks;
  std::vector<PhiInputs> phis;
  std::vector<unsigned> use_counts;
  if (budget) {
    budget->Array(blocks, graph.blocks.size()); budget->Array(phis, graph.values.size());
    budget->Array(use_counts, graph.values.size()); budget->Admit(control.nodes.size(), sizeof(SelectionRegion));
  } else {
    blocks.resize(graph.blocks.size()); phis.resize(graph.values.size()); use_counts.resize(graph.values.size());
  }
  result.reserve(control.nodes.size());
  if (budget) {
    budget->Admit(result.capacity() - control.nodes.size(), sizeof(SelectionRegion));
    budget->Step(graph.ops.size()); budget->Step(graph.values.size()); budget->Step(graph.uses.size());
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (op.life != ValueLife::kLive) continue;
    auto& block = blocks[op.block.value];
    block.previous = block.last; block.last = OpId{static_cast<std::uint32_t>(i)};
    if ((!std::holds_alternative<ValueNop>(op.operation) &&
         !std::holds_alternative<ValueJump>(op.operation)) || !Pure(op)) block.empty = false;
  }
  for (std::size_t i = 0; i < graph.values.size(); ++i) {
    const auto& value = graph.values[i];
    if (value.life != ValueLife::kLive) continue;
    if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
      auto& block = blocks[phi->block.value];
      if (block.phi) block.multiple_phis = true;
      else block.phi = ValueId{static_cast<std::uint32_t>(i)};
    }
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive) continue;
    if (const auto* value = std::get_if<ValueId>(&use.operand)) {
      // Saturation suffices: only single-use predicate ownership matters here.
      auto& count = use_counts[value->value]; if (count < 2) ++count;
    }
    if (const auto* owner = std::get_if<ValueId>(&use.owner)) {
      auto& inputs = phis[owner->value];
      if (inputs.count < inputs.uses.size()) inputs.uses[inputs.count] = UseId{static_cast<std::uint32_t>(i)};
      if (inputs.count <= inputs.uses.size()) ++inputs.count;
    }
  }
  for (const auto& node : control.nodes) {
    if (budget) budget->Step();
    if (node.decision || !node.arms || !node.next || !std::holds_alternative<std::monostate>(node.synthetic)) continue;
    const auto& header = graph.blocks[node.block.value];
    const auto& summary = blocks[node.block.value];
    if (header.edge_count != 2 || !summary.last || !summary.previous) continue;
    const auto branch_id = *summary.last, boolean_id = *summary.previous;
    const auto& branch_op = graph.ops[branch_id.value];
    const auto& boolean_op = graph.ops[boolean_id.value];
    const auto* branch = std::get_if<ValueBranch>(&branch_op.operation);
    const auto* boolean = std::get_if<ValueUnary>(&boolean_op.operation);
    if (!branch || !boolean || boolean->op != UnaryOperator::kToBoolean ||
        !Pure(branch_op) || !Pure(boolean_op) || !boolean_op.result ||
        branch_op.use_count != 1 || boolean_op.use_count != 1 ||
        use_counts[boolean_op.result->value] != 1 ||
        !SameValue(graph.uses[branch_op.first_use.value].operand, *boolean_op.result)) continue;
    const bool taken_truthy = branch->predicate == UnaryOperator::kTrueOrNonzeroNumber;
    if (!taken_truthy && branch->predicate != UnaryOperator::kFalseOrZero) continue;
    const auto* left = std::get_if<ValueId>(&graph.uses[boolean_op.first_use.value].operand);
    if (!left || !graph.values[left->value].domain.IsLanguage()) continue;
    const auto& join_node = control.nodes[node.next->value];
    if (!std::holds_alternative<std::monostate>(join_node.synthetic)) continue;
    const auto join = join_node.block;
    const auto& merged = blocks[join.value];
    if (!merged.phi || merged.multiple_phis || phis[merged.phi->value].count != 2 ||
        !graph.values[merged.phi->value].domain.IsLanguage()) continue;
    const auto taken = ReadArm(graph, control, node.arms->taken, join, header.first_edge, tails);
    const auto fallthrough = ReadArm(graph, control, node.arms->fallthrough, join, {header.first_edge.value + 1}, tails);
    if (!taken || !fallthrough) continue;
    const std::array<Arm, 2> arms{*taken, *fallthrough};
    for (unsigned keep = 0; keep < arms.size(); ++keep) {
      const auto& retained = arms[keep]; const auto& evaluated = arms[1 - keep];
      if (retained.prefix || (retained.block && (!blocks[retained.block->value].empty || blocks[retained.block->value].phi)) ||
          !evaluated.block || (blocks[evaluated.block->value].phi && !evaluated.prefix)) continue;
      const OperandUse* keep_use = nullptr; const OperandUse* evaluate_use = nullptr;
      for (const auto id : phis[merged.phi->value].uses) {
        const auto& use = graph.uses[id.value];
        if (!use.incoming) continue;
        const auto* edge = std::get_if<EdgeId>(&*use.incoming);
        if (!edge) continue;
        if (*edge == retained.incoming) keep_use = &use;
        if (*edge == evaluated.incoming) evaluate_use = &use;
      }
      if (!keep_use || !evaluate_use || !SameValue(keep_use->operand, *left)) continue;
      const bool keep_truthy = keep == 0 ? taken_truthy : !taken_truthy;
      result.push_back({keep_truthy ? TreeLogical::kOr : TreeLogical::kAnd,
          boolean_id, branch_id, *merged.phi, *left, SelectionLeaf(evaluate_use->operand),
          node.block, retained.block, *evaluated.block, join, retained.incoming, evaluated.incoming});
      result.back().evaluate_path = evaluated.prefix;
      // The existing verified ToBoolean/decision contract supplies meaning,
      // not an additional recovery admission or a native result alias.
      const std::initializer_list<SelectionPureTest> tests{
          {boolean_id,{TruthDomains::All(),TruthDomains::Truthy(),PureTruthOutcome::kBoolean}},
          {branch_id,{TruthDomains::All(),taken_truthy?TruthDomains::Truthy():TruthDomains::Truthy().Complement(),PureTruthOutcome::kDecision}}};
      if(budget)budget->Array(result.back().tests,tests.size());else result.back().tests.resize(tests.size());
      std::copy(tests.begin(),tests.end(),result.back().tests.begin());
      break;
    }
  }
  return result;
}
// Independent two-arm selection. Both arms must be single, exclusively owned
// evaluation subtrees. No arm effects are made unconditional by this projection.
namespace {
// Seed only an unresolved member Reference's argument tree. The original
// placement view also lets independent verification reconstruct these seeds
// after the interval proof has moved the callee. No names/owners are allocated.

std::vector<SelectionRegion> FindConditionalExpressions(const ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const ModuleEvaluationIndex& modules, TreeCheckBudget& budget,
    std::span<const JointSelectionUse> joints) {
  std::vector<SelectionRegion> result;
  const ReferenceArguments references(graph, evaluation, budget);
  std::optional<LiteralArmIndex> literal_arms;
  std::optional<ConstructorEventView> constructors;

  const auto literal_ends = evaluation.literals ? IndexPhiLiteralEnds(graph, *evaluation.literals, budget) :
      std::vector<std::optional<OpId>>{};
  std::vector<std::optional<ValueId>> phis;
  std::vector<std::uint8_t> multiple;
  std::vector<std::size_t> inputs;
  std::vector<std::optional<UseId>> edge_inputs;
  struct UniqueUse { std::optional<UseId> use; bool multiple = false; };
  std::vector<UniqueUse> value_uses;
  budget.Array(phis, graph.blocks.size()); budget.Array(multiple, graph.blocks.size());
  budget.Array(inputs, graph.values.size()); budget.Array(edge_inputs, graph.edges.size());
  budget.Array(value_uses, graph.values.size());
  for (std::size_t i = 0; i < graph.values.size(); ++i) {
    budget.Step(); const auto& value = graph.values[i];
    if (value.life != ValueLife::kLive) continue;
    if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
      if (phis[phi->block.value]) multiple[phi->block.value] = 1;
      else phis[phi->block.value] = ValueId{static_cast<std::uint32_t>(i)};
    }
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step(); const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive) continue;
    if (const auto* value = std::get_if<ValueId>(&use.operand)) {
      auto& used = value_uses[value->value];
      if (used.use) used.multiple = true;
      else used.use = UseId{static_cast<std::uint32_t>(i)};
    }
    if (const auto* value = std::get_if<ValueId>(&use.owner)) {
      ++inputs[value->value];
      const auto* phi = std::get_if<PhiDefinition>(&graph.values[value->value].origin);
      const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
      if (phi && edge && !multiple[phi->block.value]) edge_inputs[edge->value] = UseId{static_cast<std::uint32_t>(i)};
    }
  }
  std::vector<std::uint8_t> joint_values; if(!joints.empty())budget.Array(joint_values, graph.values.size());
  for (const auto& joint : joints) {
    budget.Step(); value_uses[joint.value.value]={joint.keep,false}; joint_values[joint.value.value]=1;
  }
  const auto retained = [&](std::optional<WorkBlockId> block, const ValueOperand& input) {
    const auto* value = std::get_if<ValueId>(&input);
    if (!value || !graph.values[value->value].domain.IsLanguage()) return false;
    const auto& origin = graph.values[value->value].origin;
    if (const auto* producer = std::get_if<OpResult>(&origin);
        producer && graph.ops[producer->op.value].block == block) return false;
    return HasRetainedEvaluationValue(graph, evaluation, *value);
  };
  const auto closed = [&](std::optional<WorkBlockId> block, const ValueOperand& input) {
    if (retained(block,input)) {
      // Availability is already proved by the input flow. An empty arm may
      // forward its snapshot, but cannot hide another effect or replay a getter.
      // A bare branch edge has no operation at all.
      if (!block) return true;
      const auto& range = graph.blocks[block->value];
      for (std::uint32_t i = 0; i < range.op_count; ++i) {
        budget.Step(); const auto& op = graph.ops[range.first_op.value + i];
        if (op.life == ValueLife::kLive &&
            (!std::holds_alternative<ValueJump>(op.operation) || !Pure(op))) return false;
      }
      return true;
    }
    const auto* value = std::get_if<ValueId>(&input);
    const auto* producer = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!block || !producer || graph.ops[producer->op.value].block != *block ||
        evaluation.operations[producer->op.value].kind != EvaluationKind::kPhiInput) return false;
    const auto literal = literal_ends.empty() ? std::nullopt : literal_ends[producer->op.value];
    const auto& range = graph.blocks[block->value];
    bool covered = true;
    for (std::uint32_t i = 0; i < range.op_count; ++i) {
      budget.Step(); const auto id = range.first_op.value + i; const auto& op = graph.ops[id];
      if (op.life != ValueLife::kLive || std::holds_alternative<ValueJump>(op.operation)) continue;
      if (literal && id >= producer->op.value && id <= literal->value) continue;
      if (id != producer->op.value && evaluation.operations[id].kind != EvaluationKind::kInline &&
          !OwnedGuard(graph, evaluation, modules, id)) covered = false;
    }
    if (covered) return true;
    if (!std::holds_alternative<ValueConstruct>(graph.ops[producer->op.value].operation)) return false;
    // Pay for the view only after old coverage fails. Fresh tree/evaluation
    // verification reconstructs the same forest; no allocation exclusion is
    // removed and no successful placement is changed.
    if (!constructors) {
      try {
        constructors = BuildConstructorEventView(graph, evaluation, budget);
        VerifyConstructorEventView(graph, evaluation, *constructors, budget);
      } catch (ConstructorEventError) { throw EvaluationError::kOwnership; }
    }
    const auto output = constructors->outputs[producer->op.value];
    const auto* merged = output ? std::get_if<ValueId>(&graph.uses[output->value].owner) : nullptr;
    if (!merged) return false;
    const auto& use = value_uses[merged->value];
    if (use.multiple || !use.use || !references.Contains(graph.uses[use.use->value])) return false;
    // COMPLETE coverage, not permission to ignore just the offending define.
    // A foreign effect, early observation or a second arm's event cannot be
    // absorbed by the constructor merely because it occurs in this block.
    for (std::uint32_t i = 0; i < range.op_count; ++i) {
      budget.Step(); const auto id = range.first_op.value + i; const auto& op = graph.ops[id];
      const auto kind = evaluation.operations[id].kind;
      if (op.life != ValueLife::kLive || kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure ||
          std::holds_alternative<ValueJump>(op.operation) || std::holds_alternative<ValueNop>(op.operation)) continue;
      if (!constructors->events[id] || constructors->events[id]->constructor != producer->op) return false;
    }
    return true;
  };
  // Children precede parents independent of block storage order. Traverse the
  // checked control forest iteratively; no per-branch whole-graph search.
  struct Frame { ControlId id; unsigned child = 0; };
  std::vector<Frame> stack;
  std::vector<ControlId> order;
  std::vector<std::uint8_t> visited;
  std::vector<std::optional<std::size_t>> selected;
  std::vector<std::size_t> leaf_counts;
  budget.Array(stack, control.nodes.size()); budget.Array(order, control.nodes.size());
  budget.Array(visited, control.nodes.size()); budget.Array(selected, control.nodes.size());
  budget.Array(leaf_counts, control.nodes.size());
  std::vector<std::uint8_t> decision_owned;
  std::vector<SelectionCondition> conditions;
  std::vector<ControlId> condition_blocks;
  std::vector<ControlId> prefix_regions;
  budget.Array(decision_owned, control.nodes.size());
  budget.Array(conditions, control.nodes.size());
  budget.Array(condition_blocks, control.nodes.size());
  budget.Array(prefix_regions, control.nodes.size());
  for (const auto& node : control.nodes) {
    budget.Step();
    if (node.decision)
      decision_owned[node.decision->predicate.value_or(node.decision->evaluate).value] = 1;
  }
  std::size_t ordered = 0;
  for (std::size_t root = 0; root < control.nodes.size(); ++root) {
    budget.Step(); if (visited[root]) continue;
    std::size_t depth = 1; stack[0] = {ControlId{static_cast<std::uint32_t>(root)}, 0}; visited[root] = 1;
    while (depth) {
      budget.Step(); auto& frame = stack[depth - 1]; const auto& node = control.nodes[frame.id.value];
      if (frame.child == 4) { order[ordered++] = frame.id; --depth; continue; }
      const auto ordinal = frame.child++;
      const auto child = ordinal == 0 ? node.next : ordinal == 3 ?
          (node.decision ? std::optional{node.decision->evaluate} : std::nullopt) :
          !node.arms ? std::nullopt : ordinal == 1 ? node.arms->taken : node.arms->fallthrough;
      if (child && !visited[child->value]) {
        visited[child->value] = 1; stack[depth++] = {*child, 0};
      }
    }
  }
  // Branch-owned pure evaluation ancestry, computed once from actual
  // consumer identities. A suffix may compose arbitrary pure conversions;
  // its register spelling and direct producer are not admission conditions.
  std::vector<std::optional<OpId>> predicate_roots;
  if(!joints.empty()) {
    std::vector<OpId> path;std::vector<std::uint8_t> state;
    budget.Array(predicate_roots,graph.ops.size());budget.Array(path,graph.ops.size());budget.Array(state,graph.ops.size());
    for(std::size_t i=0;i<graph.ops.size();++i) {
      budget.Step();if(state[i]==2)continue;OpId at{static_cast<std::uint32_t>(i)};std::size_t count=0;
      while(!state[at.value]) {
        budget.Step();state[at.value]=1;path[count++]=at;
        const auto original=BeforeLogicalFusion(graph,evaluation,at.value);
        if(original.kind!=EvaluationKind::kInline || !original.consumer || !Pure(graph.ops[at.value]))break;
        const auto* owner=std::get_if<OpId>(&graph.uses[original.consumer->value].owner);if(!owner)break;at=*owner;
      }
      std::optional<OpId> root=state[at.value]==2?predicate_roots[at.value]:
          std::holds_alternative<ValueBranch>(graph.ops[at.value].operation)?std::optional{at}:std::nullopt;
      while(count){budget.Step();const auto id=path[--count];predicate_roots[id.value]=root;state[id.value]=2;}
    }
  }
  std::vector<std::optional<std::size_t>> predicate_prefixes;
  std::vector<std::size_t> chained_prefixes;
  if(!joints.empty()){budget.Array(chained_prefixes,control.nodes.size());budget.Array(predicate_prefixes,control.nodes.size());}
  struct Path { std::optional<WorkBlockId> block; WorkBlockId join; std::optional<EdgeId> incoming; std::optional<std::size_t> child; bool suffix = false; EdgeId entry; std::optional<std::size_t> prefixes; };
  const auto path = [&](std::optional<ControlId> at, EdgeId entry) -> std::optional<Path> {
    const auto original_entry = entry;
    if (!at) {
      // The branch edge enters the join directly (the arm's register move was
      // removed). It owns no block, so closed() admits only an already
      // available phi input; the caller still checks that both arms share the join.
      const auto target = graph.edges[entry.value].target;
      if (!phis[target.value] || multiple[target.value]) return {};
      return Path{std::nullopt, target, entry, {}, false, entry};
    }
    while (at) {
      budget.Step(); const auto& node = control.nodes[at->value];
      if (!std::holds_alternative<std::monostate>(node.synthetic)) return {};
      const auto& block = graph.blocks[node.block.value];
      if (graph.edges[entry.value].target != node.block || phis[node.block.value]) return {};
      if (node.arms) {
        if (!selected[at->value]) return {};
        auto child = *selected[at->value]; const auto* current = &result[child];
        std::optional<std::size_t> prefixes;
        auto following=node.next;
        while(!joints.empty() && following && control.nodes[following->value].arms) {
          budget.Step();const auto& bridge=control.nodes[following->value];
          if(!selected[following->value] || bridge.block!=current->join || !current->complete_phi || current->parent || current->prefix_parent) return {};
          const auto next=*selected[following->value];const auto& later=result[next];
          const auto& header=graph.blocks[bridge.block.value];
          for(std::uint32_t j=0;j<header.op_count;++j) {
            budget.Step();const OpId id{header.first_op.value+j};const auto& op=graph.ops[id.value];
            const auto kind=evaluation.operations[id.value].kind;
            if(op.life!=ValueLife::kLive || id==later.branch || kind==EvaluationKind::kAlias || kind==EvaluationKind::kElidedPure)continue;
            if(!Pure(op) || predicate_roots[id.value]!=later.branch)return {};
          }
          const auto use=value_uses[current->merged.value];
          const auto* owner=use.use?std::get_if<OpId>(&graph.uses[use.use->value].owner):nullptr;
          if(!owner || use.multiple || predicate_roots[owner->value]!=later.branch) return {};
          if(predicate_prefixes[next] && predicate_prefixes[next]!=child)return {};
          predicate_prefixes[next]=child;prefixes=child;child=next;current=&later;following=bridge.next;
        }
        const auto& region=*current;
        if (region.parent || region.prefix_parent) return {};
        for (std::uint32_t i = 0; i < block.op_count; ++i) {
          budget.Step(); const auto id = block.first_op.value + i; const auto& op = graph.ops[id];
          if (op.life != ValueLife::kLive || id == result[*selected[at->value]].branch.value) continue;
          if (evaluation.operations[id].kind != EvaluationKind::kInline &&
              evaluation.operations[id].kind != EvaluationKind::kPredicateSnapshot &&
              !OwnedGuard(graph, evaluation, modules, id)) return {};
        }
        if (!following) return Path{node.block, region.join, {}, child, false, original_entry,prefixes};
        // Distinct joins remain distinct in the CFG/SSA. Only an exclusively
        // consumed child phi and either pure forwarding or one independently
        // closed suffix can enter the parent expression. Each child is examined once.
        const auto& bridge = control.nodes[following->value];
        const auto& tail = graph.blocks[bridge.block.value];
        if (!region.complete_phi || bridge.block != region.join || bridge.next || bridge.arms ||
            !std::holds_alternative<std::monostate>(bridge.synthetic) || tail.edge_count != 1 ||
            value_uses[region.merged.value].multiple) return {};
        bool suffix = false;
        for (std::uint32_t i = 0; i < tail.op_count; ++i) {
          budget.Step(); const auto& op = graph.ops[tail.first_op.value + i];
          if (op.life == ValueLife::kLive && (!std::holds_alternative<ValueJump>(op.operation) || !Pure(op))) suffix = true;
        }
        return Path{bridge.block, graph.edges[tail.first_edge.value].target, tail.first_edge, child, suffix, original_entry,prefixes};
      }
      if (block.edge_count != 1) return {};
      if (!node.next) return Path{node.block, graph.edges[block.first_edge.value].target, block.first_edge, {}, false, original_entry};
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget.Step(); const auto& op = graph.ops[block.first_op.value + i];
        if (op.life == ValueLife::kLive && (!std::holds_alternative<ValueJump>(op.operation) || !Pure(op))) return {};
      }
      entry = block.first_edge; at = node.next;
    }
    return {};
  };
  budget.Admit(control.nodes.size(), sizeof(SelectionRegion)); result.reserve(control.nodes.size());
  budget.Admit(result.capacity() - control.nodes.size(), sizeof(SelectionRegion));
  for (const auto control_id : order) {
    const auto& node = control.nodes[control_id.value];
    budget.Step(); if (decision_owned[control_id.value] || !node.arms || !std::holds_alternative<std::monostate>(node.synthetic)) continue;
    const auto& block = graph.blocks[node.block.value];
    if (block.edge_count != 2 || !block.op_count) continue;
    const OpId branch_id{block.first_op.value + block.op_count - 1};
    const auto& op = graph.ops[branch_id.value]; const auto* branch = std::get_if<ValueBranch>(&op.operation);
    if (!branch || op.life != ValueLife::kLive || op.use_count != 1 || !Pure(op)) continue;
    const bool positive = branch->predicate == UnaryOperator::kTrueOrNonzeroNumber;
    if (!positive && branch->predicate != UnaryOperator::kFalseOrZero) continue;
    const auto* predicate = std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
    const auto* producer = predicate ? std::get_if<OpResult>(&graph.values[predicate->value].origin) : nullptr;
    if (!producer || graph.ops[producer->op.value].block != node.block ||
        evaluation.operations[producer->op.value].kind != EvaluationKind::kInline ||
        evaluation.operations[producer->op.value].consumer != op.first_use) continue;
    // Numeric VM tests agree with source truthiness on Boolean results. The
    // common result proof owns this fact; an operator admission list cannot
    // define conditional syntax. Evaluation ownership still preserves all
    // getter/coercion/exception sites before entering the selected arm.
    if (!BooleanEvaluation(graph, graph.uses[op.first_use.value].operand)) continue;
    const auto* unary = std::get_if<ValueUnary>(&graph.ops[producer->op.value].operation);
    std::size_t condition_count = 0, condition_block_count = 0, prefix_count = 0;
    const auto* tail = &node;
    bool shared_taken = node.arms->taken.has_value();
    bool decision_closed = true;
    if (node.decision) {
      // Only the maximal decision owns its path. Control verification already
      // establishes single entry/shared arm; require a closed local expression
      // forest at every conditional RHS, without retained roots or phi copies.
      conditions[condition_count++] = {branch_id, shared_taken};
      while (tail->decision && decision_closed) {
        budget.Step();
        const auto& decision = *tail->decision;
        auto next = decision.evaluate;
        const auto terminal = decision.predicate.value_or(next);
        std::optional<ValueId> prefix_value;
        while (true) {
          budget.Step(); const auto& part = control.nodes[next.value];
          if ((phis[part.block.value] && phis[part.block.value] != prefix_value) ||
              multiple[part.block.value] || !std::holds_alternative<std::monostate>(part.synthetic)) { decision_closed = false; break; }
          const auto& source = graph.blocks[part.block.value];
          const bool predicate = next == terminal;
          if (!predicate && part.arms && decision.prefix == DecisionPrefix::kRegion) {
            if (!part.next || !selected[next.value]) { decision_closed = false; break; }
            const auto& prefix = result[*selected[next.value]];
            if (!prefix.complete_phi || prefix.parent || prefix.prefix_parent ||
                prefix.join != control.nodes[part.next->value].block) { decision_closed = false; break; }
            for (std::uint32_t j = 0; j < source.op_count; ++j) {
              budget.Step(); const auto id = source.first_op.value + j; const auto& operation = graph.ops[id];
              if (operation.life != ValueLife::kLive || id == prefix.branch.value) continue;
              const auto kind = evaluation.operations[id].kind;
              if (kind != EvaluationKind::kInline && kind != EvaluationKind::kPredicateSnapshot &&
                  kind != EvaluationKind::kAlias && kind != EvaluationKind::kElidedPure &&
                  !OwnedGuard(graph, evaluation, modules, id)) decision_closed = false;
            }
            if (!decision_closed) break;
            prefix_regions[prefix_count++] = next; prefix_value = prefix.merged;
            next = *part.next; continue;
          }
          if ((predicate && (!part.arms || source.edge_count != 2 || !source.op_count)) ||
              (!predicate && (part.arms || part.decision || !part.next || source.edge_count != 1))) { decision_closed = false; break; }
          for (std::uint32_t j = 0; j < source.op_count; ++j) {
            budget.Step(); const auto id = source.first_op.value + j; const auto& operation = graph.ops[id];
            if (operation.life != ValueLife::kLive || (predicate && j + 1 == source.op_count) ||
                std::holds_alternative<ValueJump>(operation.operation)) continue;
            const auto kind = evaluation.operations[id].kind;
            if (kind != EvaluationKind::kInline && kind != EvaluationKind::kAlias &&
                kind != EvaluationKind::kElidedPure && !OwnedGuard(graph, evaluation, modules, id)) decision_closed = false;
          }
          if (!decision_closed) break;
          condition_blocks[condition_block_count++] = next;
          if (predicate) {
            const OpId at{source.first_op.value + source.op_count - 1};
            const auto& last = graph.ops[at.value];
            const auto* test = std::get_if<ValueBranch>(&last.operation);
            if (!test || last.life != ValueLife::kLive || !Pure(last) || last.use_count != 1 ||
                (test->predicate != UnaryOperator::kTrueOrNonzeroNumber && test->predicate != UnaryOperator::kFalseOrZero)) { decision_closed = false; break; }
            conditions[condition_count++] = {at, decision.shared_on_taken};
            shared_taken = decision.shared_on_taken; tail = &part; break;
          }
          next = *part.next;
        }
      }
      if (!decision_closed) continue;
    }
    const auto& last = graph.blocks[tail->block.value];
    const auto taken = node.decision
        ? path(node.arms->taken ? node.arms->taken : node.arms->fallthrough,
            {block.first_edge.value + (node.arms->taken ? 0U : 1U)})
        : path(node.arms->taken, block.first_edge);
    const auto other = node.decision
        ? path(shared_taken ? tail->arms->fallthrough : tail->arms->taken,
            {last.first_edge.value + (shared_taken ? 1U : 0U)})
        : path(node.arms->fallthrough, {block.first_edge.value + 1});
    if (!taken || !other) continue;
    const auto join = taken->join;
    if (other->join != join || !phis[join.value] || multiple[join.value]) continue;
    if (node.next && (control.nodes[node.next->value].block != join ||
        !std::holds_alternative<std::monostate>(control.nodes[node.next->value].synthetic))) continue;
    const auto merged = *phis[join.value];
    if (inputs[merged.value] < 2 || !graph.values[merged.value].domain.IsLanguage()) continue;
    if (!taken->block || !other->block) {
      // A bare arm has no block for a retained assignment statement. Admit it
      // only where the selected value completes an unmet member Reference's
      // argument, whose single use the expression must own.
      const auto& used = value_uses[merged.value];
      if (used.multiple || !used.use || !references.Contains(graph.uses[used.use->value])) continue;
    }
    const auto yes = (node.decision || positive) ? *taken : *other, no = (node.decision || positive) ? *other : *taken;
    const auto operand = [&](const Path& arm) -> std::optional<SelectionOperand> {
      if (arm.child) {
        const auto& nested = result[*arm.child];
        if (nested.merged != merged) {
          if (arm.suffix) {
            if (!arm.incoming || !edge_inputs[arm.incoming->value] ||
                !value_uses[nested.merged.value].use || value_uses[nested.merged.value].multiple) return {};
            const auto& use = graph.uses[value_uses[nested.merged.value].use->value];
            const auto* owner = std::get_if<OpId>(&use.owner);
            const auto& value = graph.uses[edge_inputs[arm.incoming->value]->value].operand;
            if (!owner || graph.ops[owner->value].block != arm.block || !closed(arm.block, value)) return {};
            return SelectionLeaf(value);
          }
          if (!arm.incoming || !edge_inputs[arm.incoming->value] ||
              value_uses[nested.merged.value].use != edge_inputs[arm.incoming->value] ||
              value_uses[nested.merged.value].multiple ||
              !SameValue(graph.uses[edge_inputs[arm.incoming->value]->value].operand, nested.merged)) return {};
        } else if (arm.incoming) return {};
        return SelectionChild{nested.branch};
      }
      if (!arm.incoming || !edge_inputs[arm.incoming->value]) return {};
      const auto& value = graph.uses[edge_inputs[arm.incoming->value]->value].operand;
      if (!closed(arm.block, value)) return {};
      if (retained(arm.block,value)) return SelectionSnapshot{std::get<ValueId>(value)};
      return SelectionLeaf(value);
    };
    const auto consequent = operand(yes), alternate = operand(no);
    if (!consequent || !alternate) continue;
    if (unary && unary->op == UnaryOperator::kToBoolean) {
      const auto& boolean = graph.ops[producer->op.value];
      const auto* kept = std::get_if<SelectionSnapshot>(&*consequent);
      if (!kept) kept = std::get_if<SelectionSnapshot>(&*alternate);
      // The legacy logical owner requires an arm-local RHS expression. A
      // retained fallback has no such producer. For an unmet Reference only,
      // keep the conditional and the predicate's existing snapshot. The old
      // two-snapshot path stays unchanged; a literal arm requires its own
      // complete event-forest proof below. The interval/tree/flow checkers own
      // movement, truth/phi correspondence and single evaluation. This does
      // not widen the deferred joint predicate/keep-edge consumer admission.
      if (kept && boolean.use_count == 1 &&
          SameValue(graph.uses[boolean.first_use.value].operand, kept->value)) {
        const auto& used = value_uses[merged.value];
        if (used.multiple || !used.use || ((joint_values.empty() || !joint_values[merged.value]) && !references.Contains(graph.uses[used.use->value]))) continue;
        if (!std::holds_alternative<SelectionSnapshot>(*consequent) ||
            !std::holds_alternative<SelectionSnapshot>(*alternate)) {
          // The saved member read, not the argument allocation, is the unmet
          // Reference. Preserve the predicate snapshot and BOTH original phi
          // edges. Each non-retained arm must own its entire literal forest;
          // the normal interval proof then composes the saved callee, this
          // conditional event and the invocation. No joint keep-edge admission.
          if (!evaluation.literals) continue;
          if (!literal_arms) {
            try { literal_arms.emplace(graph, evaluation, budget); }
            catch (LiteralEventError) { throw EvaluationError::kOwnership; }
          }
          const auto proven = [&](const Path& arm, const SelectionOperand& value) {
            if (std::holds_alternative<SelectionSnapshot>(value)) return true;
            return arm.incoming && edge_inputs[arm.incoming->value] &&
                std::holds_alternative<LiteralArmView>(literal_arms->Prove(
                    arm.entry, *edge_inputs[arm.incoming->value]));
          };
          if (!proven(yes, *consequent) || !proven(no, *alternate)) continue;
        }
      }
    }
    const auto leaves = (yes.child && !yes.incoming ? leaf_counts[*yes.child] : 1) +
        (no.child && !no.incoming ? leaf_counts[*no.child] : 1);
    auto dependency = branch_id;
    for (const auto& arm : {yes, no}) {
      if (arm.child) dependency = std::max(dependency, *result[*arm.child].last_dependency);
      if (!arm.child || arm.suffix) {
        const auto value = std::get<ValueId>(graph.uses[edge_inputs[arm.incoming->value]->value].operand);
        if (retained(arm.block,value)) continue;
        const auto source = std::get<OpResult>(graph.values[value.value].origin).op;
        if (!IsSelectionConstant(graph, source)) {
          const auto literal = literal_ends.empty() ? std::nullopt : literal_ends[source.value];
          dependency = std::max(dependency, literal.value_or(source));
        }
      }
    }
    selected[control_id.value] = result.size(); leaf_counts[result.size()] = leaves;
    for (const auto& arm : {yes, no}) if (arm.child) {
      auto& nested = result[*arm.child];
      if (arm.suffix) nested.prefix_parent = branch_id;
      else {
        nested.parent = branch_id;
        if (arm.incoming) nested.forwarded = edge_inputs[arm.incoming->value];
      }
    }
    result.push_back({TreeLogical::kOr, producer->op, branch_id, merged, *predicate, *alternate,
        node.block, yes.block, no.block, join, yes.incoming, no.incoming, *consequent,
        node.decision ? (node.arms->taken ? node.arms->taken : node.arms->fallthrough) : positive ? node.arms->taken : node.arms->fallthrough,
        node.decision ? (shared_taken ? tail->arms->fallthrough : tail->arms->taken) : positive ? node.arms->fallthrough : node.arms->taken,
        leaves == inputs[merged.value] && node.next.has_value(), {}, dependency});
    std::size_t chained_count=0;
    for(const auto& arm:{yes,no}) for(auto prefix=arm.prefixes;prefix;prefix=predicate_prefixes[*prefix]) {
      budget.Step();RequireTree(chained_count<chained_prefixes.size(),TreeStructureError::kOwnership);
      chained_prefixes[chained_count++]=*prefix;
    }
    const auto arm_prefix_count = static_cast<std::size_t>(yes.suffix) + static_cast<std::size_t>(no.suffix)+chained_count;
    if (arm_prefix_count) {
      auto& prefixes = result.back().arm_prefixes; budget.Array(prefixes, arm_prefix_count);
      std::size_t at = 0;
      for (const auto& arm : {yes, no}) if (arm.suffix) prefixes[at++] = result[*arm.child].branch;
      for(std::size_t i=0;i<chained_count;++i) {
        budget.Step();auto& prefix=result[chained_prefixes[i]];
        RequireTree(!prefix.parent && !prefix.prefix_parent,TreeStructureError::kOwnership);
        prefix.prefix_parent=branch_id;prefixes[at++]=prefix.branch;
      }
    }
    if (node.decision) {
      auto& region = result.back();
      budget.Array(region.conditions, condition_count);
      budget.Array(region.condition_blocks, condition_block_count);
      budget.Array(region.prefix_regions, prefix_count);
      std::copy_n(conditions.begin(), condition_count, region.conditions.begin());
      std::copy_n(condition_blocks.begin(), condition_block_count, region.condition_blocks.begin());
      std::copy_n(prefix_regions.begin(), prefix_count, region.prefix_regions.begin());
      for (const auto prefix : region.prefix_regions) {
        budget.Step(); result[*selected[prefix.value]].prefix_parent = branch_id;
      }
      region.last_dependency = std::max(*region.last_dependency, conditions[condition_count - 1].branch);
    }
  }
  return result;
}
}  // namespace
std::vector<SelectionRegion> FindSelectionExpressions(const ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget, const ExceptionControl* projection,
    std::span<const JointSelectionUse> joints) {
  if (!graph.phases.empty() && control.view == ControlViewKind::kProjection &&
      evaluation.flow == EvaluationFlow::kPhased && control.revision == graph.revision &&
      control.cfg_revision == graph.cfg_revision && evaluation.revision == graph.revision &&
      evaluation.cfg_revision == graph.cfg_revision && evaluation.operations.size() == graph.ops.size()) {
    auto regions = FindPhasedSelections(graph, control, evaluation, budget, projection);
    ProjectSelectionLogicals(graph, control, evaluation, regions, budget);
    return regions;
  }
  if (!graph.phases.empty() || control.view != ControlViewKind::kNormal ||
      control.revision != graph.revision || control.cfg_revision != graph.cfg_revision ||
      evaluation.flow != EvaluationFlow::kNormal || evaluation.operations.size() != graph.ops.size() ||
      evaluation.revision != graph.revision || evaluation.cfg_revision != graph.cfg_revision) return {};
  // One peer index for all selected arms, reusing the common exact census,
  // adjacency, operand and effect checker. No per-branch module search.
  const auto indexed = IndexModuleEvaluations(graph, evaluation.modules, budget.Remaining());
  const auto* modules = std::get_if<ModuleEvaluationIndex>(&indexed);
  if (!modules) {
    const auto error = std::get<EvaluationFailure>(indexed);
    if (error == EvaluationError::kResourceLimit) throw TreeStructureError::kResourceLimit;
    if (error == EvaluationError::kWorkLimit) throw TreeStructureError::kWorkLimit;
    return {};
  }
  budget.Admit(modules->allocated_bytes, 1); budget.Step(modules->work);
  if (joints.empty()) joints = evaluation.joint_selection_uses;
  auto conditional = FindConditionalExpressions(graph, control, evaluation, *modules, budget, joints);
  std::vector<std::optional<std::size_t>> selected;
  std::vector<OpId> prefix_branches;
  budget.Array(selected, graph.ops.size()); budget.Array(prefix_branches, control.nodes.size());
  for (std::size_t i = 0; i < conditional.size(); ++i) {
    budget.Step(); selected[conditional[i].branch.value] = i;
  }
  auto regions = FindLogicalRegions(graph, control, &budget);
  std::size_t count = 0;
  for (auto& region : regions) {
    budget.Step();
    const auto& boolean = evaluation.operations[region.boolean.value];
    if (boolean.kind != EvaluationKind::kInline ||
        boolean.consumer != graph.ops[region.branch.value].first_use) continue;
    const auto* right = std::get_if<ValueId>(&region.right);
    const auto* producer = right ? std::get_if<OpResult>(&graph.values[right->value].origin) : nullptr;
    if (!producer || !region.evaluate || graph.ops[producer->op.value].block != *region.evaluate ||
        evaluation.operations[producer->op.value].kind != EvaluationKind::kPhiInput) continue;
    const auto& block = graph.blocks[region.evaluate->value];
    bool closed = true;
    std::size_t prefix_count = 0;
    auto prefix = region.evaluate_path;
    while (prefix && control.nodes[prefix->value].block != *region.evaluate) {
      budget.Step(); const auto& node = control.nodes[prefix->value];
      const auto& range = graph.blocks[node.block.value];
      if (!node.next || !std::holds_alternative<std::monostate>(node.synthetic)) { closed = false; break; }
      std::optional<OpId> branch;
      if (node.arms && range.op_count) {
        branch = OpId{range.first_op.value + range.op_count - 1};
        if (!selected[branch->value]) { closed = false; break; }
        const auto& child = conditional[*selected[branch->value]];
        if (!child.complete_phi || child.parent || child.prefix_parent ||
            child.join != control.nodes[node.next->value].block) { closed = false; break; }
        prefix_branches[prefix_count++] = *branch;
      }
      for (std::uint32_t i = 0; i < range.op_count; ++i) {
        budget.Step(); const OpId id{range.first_op.value + i}; const auto& op = graph.ops[id.value];
        if (op.life != ValueLife::kLive || id == branch || std::holds_alternative<ValueJump>(op.operation)) continue;
        const auto kind = evaluation.operations[id.value].kind;
        if (kind != EvaluationKind::kInline && kind != EvaluationKind::kPredicateSnapshot &&
            kind != EvaluationKind::kAlias && kind != EvaluationKind::kElidedPure &&
            !OwnedGuard(graph, evaluation, *modules, id.value)) closed = false;
      }
      if (!closed) break;
      prefix = node.next;
    }
    for (std::uint32_t j = 0; j < block.op_count; ++j) {
      budget.Step(); const auto id = block.first_op.value + j; const auto& op = graph.ops[id];
      if (op.life != ValueLife::kLive || std::holds_alternative<ValueJump>(op.operation)) continue;
      if (evaluation.operations[id].kind != EvaluationKind::kInline && id != producer->op.value &&
          !OwnedGuard(graph, evaluation, *modules, id)) closed = false;
    }
    if (region.keep) {
      const auto& keep = graph.blocks[region.keep->value];
      for (std::uint32_t j = 0; j < keep.op_count; ++j) {
        budget.Step(); const auto& op = graph.ops[keep.first_op.value + j];
        if (op.life == ValueLife::kLive && !std::holds_alternative<ValueJump>(op.operation)) closed = false;
      }
    }
    if (closed) {
      budget.Array(region.arm_prefixes, prefix_count);
      std::copy_n(prefix_branches.begin(), prefix_count, region.arm_prefixes.begin());
      for (const auto branch : region.arm_prefixes) {
        budget.Step(); conditional[*selected[branch.value]].prefix_parent = region.branch;
      }
      if (count != static_cast<std::size_t>(&region - regions.data())) regions[count] = std::move(region);
      ++count;
    }
  }
  regions.resize(count);
  budget.Admit(conditional.size(), sizeof(SelectionRegion));
  regions.insert(regions.end(), std::make_move_iterator(conditional.begin()), std::make_move_iterator(conditional.end()));
  ProjectSelectionLogicals(graph, control, evaluation, regions, budget, joints);
  return regions;
}
std::vector<std::optional<UseId>> FindLogicalLeftInputs(const ValueProgram& graph,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget) {
  std::vector<std::optional<UseId>> inputs;
  if (evaluation.flow != EvaluationFlow::kNormal || !evaluation.phis) return inputs;
  // Most phi-bearing methods have no duplicated logical producer. Avoid
  // constructing another control plan when no local ToBoolean can own one.
  bool candidate = false;
  for (const auto& op : graph.ops) {
    budget.Step();
    const auto* unary = std::get_if<ValueUnary>(&op.operation);
    if (op.life != ValueLife::kLive || !unary || unary->op != UnaryOperator::kToBoolean || op.use_count != 1) continue;
    const auto* value = std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
    const auto* result = value ? std::get_if<OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!result || graph.ops[result->op.value].block != op.block) continue;
    const auto kind = BeforeLogicalFusion(graph, evaluation, result->op.value).kind;
    if (kind == EvaluationKind::kRoot || kind == EvaluationKind::kLogicalLeft) { candidate = true; break; }
  }
  if (!candidate) return inputs;
  auto built = BuildControlPlan(graph, budget.Remaining());
  const auto* control = std::get_if<ControlPlan>(&built);
  if (!control) {
    const auto error = ControlResultError(built);
    if (error == ControlError::kResourceLimit) throw TreeStructureError::kResourceLimit;
    if (error == ControlError::kWorkLimit) throw TreeStructureError::kWorkLimit;
    return inputs;
  }
  budget.Admit(control->allocated_bytes, 1); budget.Step(control->work);
  const auto regions = FindSelectionExpressions(graph, *control, evaluation, budget);
  if (regions.empty()) return inputs;
  budget.Array(inputs, graph.ops.size());
  for (const auto& region : regions) {
    budget.Step(); if (region.consequent) continue;
    const auto& value = graph.values[region.left.value];
    const auto* result = std::get_if<OpResult>(&value.origin);
    if (!result) continue;
    const auto& op = graph.ops[result->op.value];
    const auto placement = BeforeLogicalFusion(graph, evaluation, result->op.value);
    if (op.block != region.header || op.order >= graph.ops[region.boolean.value].order ||
        (placement.kind != EvaluationKind::kRoot && placement.kind != EvaluationKind::kLogicalLeft)) continue;
    const auto predicate = graph.ops[region.boolean.value].first_use;
    bool saw_predicate = false, saw_keep = false, exclusive = true;
    // Eligible producers belong to distinct headers. Their use chains and
    // trailing header intervals are disjoint; no per-region graph search.
    for (auto id = value.first_use; id; id = graph.uses[id->value].next) {
      budget.Step(); const auto& use = graph.uses[id->value];
      if (use.life != ValueLife::kLive) continue;
      if (*id == predicate) { saw_predicate = true; continue; }
      const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
      if (use.owner == decltype(use.owner){region.merged} && edge && *edge == region.keep_input &&
          use.role == OperandRole::kPhiInput && use.purpose == ReadPurpose::kOperand && !saw_keep)
        saw_keep = true;
      else exclusive = false;
    }
    if (!exclusive || !saw_predicate || !saw_keep) continue;
    bool clear = true;
    for (auto i = result->op.value + 1; i < region.boolean.value; ++i) {
      budget.Step(); const auto& between = graph.ops[i];
      if (between.life != ValueLife::kLive ||
          evaluation.operations[i].kind == EvaluationKind::kCellGuard) continue;
      if (OrderedEvaluation(between) || IsEnvironmentState(between.operation)) clear = false;
    }
    if (clear) inputs[result->op.value] = predicate;
  }
  return inputs;
}
}  // namespace abcd::source
