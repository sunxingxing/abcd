#include "logical_regions.h"
#include "evaluation_plan.h"
#include "tree_checks.h"
#include "truth_domains.h"
#include <algorithm>

namespace abcd::source {
namespace {
using namespace analysis;
// ECMA-262 ToBoolean and ?? need these four disjoint language domains. The
// other domains include all Number/BigInt/string/object/Symbol values; no
// numeric safety fact or user conversion is inferred from this partition.
using TruthMask = TruthDomains;
constexpr TruthMask Bit(TruthDomain domain) { return TruthMask::One(domain); }
constexpr TruthMask kAll = TruthMask::All();
constexpr TruthMask kTruthy = Bit(TruthDomain::kTruthy);
constexpr TruthMask kOther = Bit(TruthDomain::kFalsy) | kTruthy;
enum class Predicate : std::uint8_t { kCopy, kBoolean, kNot, kNull, kUndefined };
struct Truth {
  ValueId value;
  TruthMask mask;
  UseId occurrence;
  bool boolean_result;
};
struct Equation {
  ValueId input;
  OpId owner;
  Predicate predicate;
  bool inverted;
  UseId occurrence;
};
Truth Apply(const Equation& equation, Truth input) {
  switch (equation.predicate) {
    case Predicate::kCopy: break;
    case Predicate::kBoolean: input.boolean_result = true; break;
    case Predicate::kNot: input.mask ^= kAll; input.boolean_result = true; break;
    case Predicate::kNull:
      input.mask = input.boolean_result ? TruthMask{} : Bit(TruthDomain::kNull); input.boolean_result = true; break;
    case Predicate::kUndefined:
      input.mask = input.boolean_result ? TruthMask{} : Bit(TruthDomain::kUndefined); input.boolean_result = true; break;
  }
  if (equation.inverted) input.mask ^= kAll;
  return input;
}
std::optional<bool> TruthPolarity(const ValueProgram& graph, UnaryOperator op, const ValueOperand& input) {
  switch (op) {
    case UnaryOperator::kToBoolean: return true;
    case UnaryOperator::kLogicalNot: return false;
    // VM numeric predicates differ from JS truthiness for null/undefined/NaN.
    case UnaryOperator::kFalseOrZero:
      if (BooleanEvaluation(graph, input)) return false;
      return {};
    case UnaryOperator::kTrueOrNonzeroNumber:
      if (BooleanEvaluation(graph, input)) return true;
      return {};
    case UnaryOperator::kTemplateObject:
    case UnaryOperator::kLegacyNegate: case UnaryOperator::kNegate:
    case UnaryOperator::kBitNot: case UnaryOperator::kTypeOf:
    case UnaryOperator::kToNumber: case UnaryOperator::kToNumeric:
    case UnaryOperator::kIncrement: case UnaryOperator::kDecrement: return {};
  }
  return {};
}
// One-consumer evaluation edges form a forest. Resolve roots with iterative
// path compression, not OpId order, block adjacency or a per-value walk.
std::vector<OpId> EvaluationRoots(const ValueProgram& graph,
    const EvaluationPlan& evaluation, TreeCheckBudget& budget) {
  std::vector<OpId> roots, path;
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<Visit> state;
  budget.Array(roots, graph.ops.size()); budget.Array(path, graph.ops.size());
  budget.Array(state, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (state[i] == Visit::kDone) continue;
    OpId cursor{static_cast<std::uint32_t>(i)};
    std::size_t count = 0;
    while (state[cursor.value] == Visit::kUnseen) {
      budget.Step(); state[cursor.value] = Visit::kActive; path[count++] = cursor;
      const auto& placement = evaluation.operations[cursor.value];
      if (placement.kind != EvaluationKind::kInline) break;
      RequireTree(placement.consumer && placement.consumer->value < graph.uses.size(), TreeStructureError::kOwnership);
      const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
      RequireTree(owner && owner->value < graph.ops.size(), TreeStructureError::kOwnership);
      cursor = *owner;
    }
    const auto root = state[cursor.value] == Visit::kDone ? roots[cursor.value] : cursor;
    RequireTree(evaluation.operations[cursor.value].kind != EvaluationKind::kInline || state[cursor.value] == Visit::kDone,
        TreeStructureError::kOwnership);
    while (count) {
      budget.Step(); const auto id = path[--count]; roots[id.value] = root; state[id.value] = Visit::kDone;
    }
  }
  return roots;
}
}  // namespace

std::optional<PureTruthTest> PureBranchTruth(const analysis::ValueProgram& graph,analysis::OpId id) {
  RequireTree(id.value<graph.ops.size(),TreeStructureError::kOwnership);
  const auto& operation=graph.ops[id.value];
  const auto* branch=std::get_if<analysis::ValueBranch>(&operation.operation);
  if(!branch||operation.life!=analysis::ValueLife::kLive||operation.use_count!=1)return {};
  RequireTree(operation.first_use.value<graph.uses.size(),TreeStructureError::kOwnership);
  const auto polarity=TruthPolarity(graph,branch->predicate,graph.uses[operation.first_use.value].operand);
  if(!polarity)return {};
  return PureTruthTest{TruthDomains::All(),*polarity?TruthDomains::Truthy():TruthDomains::Truthy().Complement(),PureTruthOutcome::kDecision};
}

void ProjectSelectionLogicals(const ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, std::span<SelectionRegion> regions, TreeCheckBudget& budget,
    std::span<const JointSelectionUse> joints) {
  if (regions.empty()) return;
  const auto roots = EvaluationRoots(graph, evaluation, budget);
  std::vector<std::optional<Equation>> equations;
  std::vector<std::optional<Truth>> truths;
  std::vector<std::optional<ValueId>> dependents, next;
  std::vector<ValueId> queue;
  std::vector<OpId> path;
  std::vector<SelectionPureTest> tests;
  std::vector<std::optional<bool>> pure_blocks;
  budget.Array(equations, graph.values.size()); budget.Array(truths, graph.values.size());
  budget.Array(dependents, graph.values.size()); budget.Array(next, graph.values.size());
  budget.Array(queue, graph.values.size()); budget.Array(path, graph.ops.size());
  budget.Array(tests,graph.ops.size());
  budget.Array(pure_blocks, graph.blocks.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto& op = graph.ops[i];
    if (op.life != ValueLife::kLive || !op.result ||
        evaluation.operations[i].kind != EvaluationKind::kInline ||
        op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
        op.exceptional != ExceptionalCompletion::kImpossible ||
        !std::holds_alternative<ValueBranch>(graph.ops[roots[i].value].operation)) continue;
    Predicate predicate = Predicate::kCopy; bool inverted = false;
    std::optional<UseId> occurrence;
    if (const auto* binary = std::get_if<ValueBinary>(&op.operation)) {
      if (op.use_count != 2 || (binary->op != BinaryOperator::kStrictEqual && binary->op != BinaryOperator::kStrictNotEqual)) continue;
      for (unsigned i = 0; i < op.use_count; ++i) {
        const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value + i].operand);
        const auto* constant = std::get_if<SpecialValue>(&operand);
        if (!constant || (*constant != SpecialValue::kNull && *constant != SpecialValue::kUndefined)) continue;
        predicate = *constant == SpecialValue::kNull ? Predicate::kNull : Predicate::kUndefined;
        occurrence = UseId{op.first_use.value + 1 - i}; break;
      }
      if (!occurrence) continue;
      inverted = binary->op == BinaryOperator::kStrictNotEqual;
    } else if (op.use_count == 1) occurrence = op.first_use;
    else continue;
    const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[occurrence->value].operand);
    const auto* input = std::get_if<ValueId>(&operand);
    if (!input || !graph.values[input->value].domain.IsLanguage()) continue;
    if (const auto* unary = std::get_if<ValueUnary>(&op.operation)) {
      const auto polarity = TruthPolarity(graph, unary->op, operand);
      if (!polarity) continue;
      predicate = *polarity ? Predicate::kBoolean : Predicate::kNot;
    } else if (!std::holds_alternative<ValueCopy>(op.operation) && !std::holds_alternative<ValueBinary>(op.operation)) continue;
    equations[op.result->value] = Equation{*input, roots[i], predicate, inverted, *occurrence};
  }
  std::size_t pending = 0;
  for (std::size_t i = 0; i < equations.size(); ++i) {
    budget.Step(); if (!equations[i]) continue;
    const auto& equation = *equations[i]; const auto& input = equations[equation.input.value];
    if (input && input->owner == equation.owner) {
      next[i] = dependents[equation.input.value]; dependents[equation.input.value] = ValueId{static_cast<std::uint32_t>(i)};
    } else {
      truths[i] = Apply(equation, Truth{equation.input, kTruthy, equation.occurrence, false});
      queue[pending++] = {static_cast<std::uint32_t>(i)};
    }
  }
  for (std::size_t i = 0; i < pending; ++i) {
    budget.Step(); const auto value = queue[i];
    for (auto child = dependents[value.value]; child; child = next[child->value]) {
      budget.Step(); const auto& equation = *equations[child->value];
      truths[child->value] = Apply(equation, *truths[value.value]);
      queue[pending++] = *child;
    }
  }
  const auto pure = [&](WorkBlockId id) {
    auto& cached = pure_blocks[id.value]; if (cached) return *cached;
    bool result = true; const auto& block = graph.blocks[id.value];
    for (std::uint32_t i = 0; i < block.op_count; ++i) {
      budget.Step(); const auto at = block.first_op.value + i; const auto& op = graph.ops[at];
      if (op.life != ValueLife::kLive || std::holds_alternative<ValueJump>(op.operation) ||
          std::holds_alternative<ValueBranch>(op.operation)) continue;
      const auto kind = evaluation.operations[at].kind;
      if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure) continue;
      if (!op.result || !equations[op.result->value]) result = false;
    }
    cached = result; return result;
  };
  for (auto& region : regions) {
    budget.Step(); if (!region.consequent) continue;
    const auto* yes = std::get_if<SelectionSnapshot>(&*region.consequent);
    const auto* no = std::get_if<SelectionSnapshot>(&region.right);
    if (!yes && !no) continue;
    bool closed = true;
    RequireTree(graph.phases.empty() || region.condition_phases.size() == region.condition_blocks.size(),
        TreeStructureError::kOwnership);
    for (std::size_t i = 0; i < region.condition_blocks.size(); ++i) {
      budget.Step();
      // The phased adapter has already bound these original phase identities
      // to control owners. A projected control-unit index is not a block ID.
      const auto block = graph.phases.empty()
          ? control.nodes[region.condition_blocks[i].value].block
          : region.condition_phases[i];
      if (!pure(block)) closed = false;
    }
    if (!closed) continue;
    std::optional<Truth> condition;
    std::size_t count = 0,test_count=0;auto arrival=kAll;
    const auto scalar = [&](OpId branch, bool on_taken) {
      budget.Step(); const auto& op = graph.ops[branch.value];
      if (op.use_count != 1) return false;
      const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value].operand);
      const auto* value = std::get_if<ValueId>(&operand);
      if (!value || !BooleanEvaluation(graph, operand) || !equations[value->value] ||
          equations[value->value]->owner != branch || !truths[value->value]) return false;
      const auto polarity = TruthPolarity(graph, std::get<ValueBranch>(op.operation).predicate, operand);
      if (!polarity) return false;
      auto truth = *truths[value->value];
      const auto taken=*polarity?truth.mask:truth.mask.Complement();
      RequireTree(test_count<tests.size(),TreeStructureError::kOwnership);
      tests[test_count++]={branch,{arrival,arrival&taken,PureTruthOutcome::kDecision}};
      if (*polarity != on_taken) truth.mask ^= kAll;
      if (condition && condition->value != truth.value) return false;
      if (!condition) condition = truth;
      else condition->mask |= truth.mask;
      auto cursor = *value;
      while (equations[cursor.value] && equations[cursor.value]->owner == branch) {
        budget.Step(); const auto* result = std::get_if<OpResult>(&graph.values[cursor.value].origin);
        RequireTree(result && count < path.size(), TreeStructureError::kOwnership);
        path[count++] = result->op;
        RequireTree(truths[cursor.value].has_value()&&test_count<tests.size(),TreeStructureError::kOwnership);
        const auto& value_truth=*truths[cursor.value];
        tests[test_count++]={result->op,{arrival,arrival&value_truth.mask,
            value_truth.boolean_result?PureTruthOutcome::kBoolean:PureTruthOutcome::kInput}};
        cursor = equations[cursor.value]->input;
      }
      arrival=arrival&truth.mask.Complement();
      return true;
    };
    if (region.conditions.empty()) closed = scalar(region.branch, true);
    else for (const auto& test : region.conditions) {
      if (!scalar(test.branch, test.shared_on_taken)) { closed = false; break; }
    }
    if (!closed || !condition) continue;
    const auto matches = [&](const SelectionSnapshot* kept) {
      return kept && ResolveEvaluationValue(evaluation, kept->value) == condition->value;
    };
    const bool keep_yes = matches(yes), keep_no = matches(no);
    // Identical arms have a separate constant-selection simplification. Keep
    // their original representation here; neither arm supplies a logical RHS.
    if (keep_yes == keep_no) continue;
    const auto kept = keep_yes ? condition->mask : TruthMask(condition->mask ^ kAll);
    std::optional<TreeLogical> operation;
    if (kept == kTruthy) operation = TreeLogical::kOr;
    else if (kept == TruthMask(kTruthy ^ kAll)) operation = TreeLogical::kAnd;
    else if (kept == kOther) operation = TreeLogical::kNullish;
    if (!operation) continue;
    SelectionLogical logical{*operation,
        condition->value, condition->occurrence, keep_yes, {}};
    budget.Array(logical.truths, count); std::copy_n(path.begin(), count, logical.truths.begin());
    budget.Array(region.tests,test_count);std::copy_n(tests.begin(),test_count,region.tests.begin());
    region.logical = std::move(logical);
  }
  if (joints.empty() || !graph.phases.empty()) return;
  // Joint Reference seeds alone authorize this additional projection. A
  // shared-phi decision is a four-domain function of ONE retained value, not
  // a new consumer exemption. Walk disjoint original region trees once.
  std::vector<std::optional<std::size_t>> branches, joint_values;
  std::vector<std::optional<Truth>> branch_truths;
  std::vector<std::uint8_t> visited;
  struct Task { std::size_t region; TruthMask arrival; };
  std::vector<Task> tasks, members;
  budget.Array(branches, graph.ops.size());
  budget.Array(branch_truths, graph.ops.size());
  budget.Array(joint_values, graph.values.size());
  budget.Array(visited, regions.size());
  budget.Array(tasks, regions.size()); budget.Array(members, regions.size());
  for (std::size_t i = 0; i < regions.size(); ++i) {
    budget.Step(); branches[regions[i].branch.value] = i;
    const auto& op = graph.ops[regions[i].branch.value];
    if (op.use_count != 1) continue;
    const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value].operand);
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value || !BooleanEvaluation(graph, operand) || !equations[value->value] ||
        equations[value->value]->owner != regions[i].branch || !truths[value->value]) continue;
    const auto polarity = TruthPolarity(graph, std::get<ValueBranch>(op.operation).predicate, operand);
    if (!polarity) continue;
    branch_truths[regions[i].branch.value] = *truths[value->value];
    if (!*polarity) branch_truths[regions[i].branch.value]->mask ^= kAll;
  }
  for (std::size_t i = 0; i < joints.size(); ++i) {
    budget.Step(); joint_values[joints[i].value.value] = i;
  }
  const auto same = [](const ValueOperand& a, const ValueOperand& b) {
    if (a.index() != b.index()) return false;
    return std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, analysis::SpecialValue>) return value == std::get<T>(b);
      else return value.value == std::get<T>(b).value;
    }, a);
  };
  const auto constant = [&](ValueId value) -> std::optional<ValueOperand> {
    const auto input = ResolveEvaluationOperand(evaluation, value);
    if (!std::holds_alternative<ValueId>(input)) return input;
    const auto* result = std::get_if<OpResult>(&graph.values[std::get<ValueId>(input).value].origin);
    if (!result) return {};
    const auto& op = graph.ops[result->op.value];
    if (!IsSelectionConstant(graph, result->op)) return {};
    return graph.uses[op.first_use.value].operand;
  };
  for (std::size_t root = 0; root < regions.size(); ++root) {
    budget.Step(); const auto& first = regions[root];
    const auto truth = branch_truths[first.branch.value];
    if (first.parent || !first.complete_phi || first.logical || !first.consequent ||
        !truth || !joint_values[truth->value.value]) continue;
    const auto& joint = joints[*joint_values[truth->value.value]];
    std::size_t pending = 1, count = 0; tasks[0] = {root, kAll};
    std::optional<SelectionOperand> fallback;
    std::optional<ValueOperand> fallback_constant;
    TruthMask retained_domains, fallback_domains;
    bool closed = true; bool kept = false;
    while (pending && closed) {
      budget.Step(); const auto task = tasks[--pending]; auto& region = regions[task.region];
      const auto scalar = branch_truths[region.branch.value];
      if (visited[task.region] || !scalar || scalar->value != truth->value ||
          !region.consequent || !region.conditions.empty() ||
          region.merged != first.merged || !region.prefix_regions.empty() ||
          !region.arm_prefixes.empty() || !pure(region.header)) { closed = false; break; }
      visited[task.region] = 1; members[count++] = task;
      // Consequent is the Boolean predicate's true arm, independently of VM
      // branch polarity. Its domain therefore uses the original truth value.
      const auto predicate = ResolveEvaluationOperand(evaluation,
          graph.uses[graph.ops[region.branch.value].first_use.value].operand);
      const auto yes_domains = truths[std::get<ValueId>(predicate).value]->mask;
      const auto leaf = [&](const SelectionOperand& operand, TruthMask domain,
                            std::optional<EdgeId> edge) {
        if (const auto* child = std::get_if<SelectionChild>(&operand)) {
          const auto child_index = branches[child->branch.value];
          if (!child_index || regions[*child_index].parent != region.branch ||
              pending >= tasks.size()) { closed = false; return; }
          tasks[pending++] = {*child_index, domain}; return;
        }
        if (!edge) { closed = false; return; }
        std::optional<ValueOperand> meaning;
        if (const auto* snapshot = std::get_if<SelectionSnapshot>(&operand)) {
          const auto value = ResolveEvaluationValue(evaluation, snapshot->value);
          if (value == truth->value) {
            const auto& keep = graph.uses[joint.keep.value];
            const auto* incoming = keep.incoming ? std::get_if<EdgeId>(&*keep.incoming) : nullptr;
            if (!incoming || *incoming != *edge) { closed = false; return; }
            retained_domains |= domain; kept = true; return;
          }
          meaning = ResolveEvaluationOperand(evaluation, value);
        } else if (const auto* value = std::get_if<ValueId>(&operand)) {
          meaning = constant(*value);
        }
        if (!meaning || (fallback_constant && !same(*fallback_constant, *meaning))) {
          closed = false; return;
        }
        fallback = operand; fallback_constant = meaning; fallback_domains |= domain;
      };
      leaf(*region.consequent, task.arrival & yes_domains, region.keep_input);
      leaf(region.right, task.arrival & yes_domains.Complement(), region.evaluate_input);
    }
    if (!closed || !kept || !fallback || retained_domains != kOther ||
        fallback_domains != TruthDomains::Nullish()) continue;
    // Commit only after every original leaf is covered. Each scalar keeps its
    // arrival domain so recovery can match the shared native nullish decision
    // without inventing an unconditional second read or dropping a phi edge.
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); const auto task = members[i]; auto& region = regions[task.region];
      const auto& scalar = *branch_truths[region.branch.value];
      SelectionLogical logical{TreeLogical::kNullish, truth->value, truth->occurrence, false, {}};
      logical.shared_owner = task.region == root ? std::nullopt : std::optional{first.branch};
      logical.shared_right = fallback; logical.shared_keep = joint.keep;
      std::size_t size = 0, test_size = 0;
      tests[test_size++] = {region.branch, {task.arrival, task.arrival & scalar.mask, PureTruthOutcome::kDecision}};
      auto cursor = std::get<ValueId>(ResolveEvaluationOperand(evaluation,
          graph.uses[graph.ops[region.branch.value].first_use.value].operand));
      while (equations[cursor.value] && equations[cursor.value]->owner == region.branch) {
        budget.Step(); const auto op = std::get<OpResult>(graph.values[cursor.value].origin).op;
        path[size++] = op; const auto& derived = *truths[cursor.value];
        tests[test_size++] = {op, {task.arrival, task.arrival & derived.mask,
            derived.boolean_result ? PureTruthOutcome::kBoolean : PureTruthOutcome::kInput}};
        cursor = equations[cursor.value]->input;
      }
      budget.Array(logical.truths, size); std::copy_n(path.begin(), size, logical.truths.begin());
      budget.Array(region.tests, test_size); std::copy_n(tests.begin(), test_size, region.tests.begin());
      if (task.region == root) {
        budget.Array(logical.shared_branches, count - 1);
        for (std::size_t j = 1; j < count; ++j) {
          budget.Step(); logical.shared_branches[j - 1] = regions[members[j].region].branch;
        }
      }
      region.logical = std::move(logical);
    }
  }

}
}  // namespace abcd::source
