#include "logical_regions.h"
#include "evaluation_plan.h"
#include "tree_checks.h"
#include <algorithm>

namespace abcd::source {
namespace {
using namespace analysis;
struct Truth {
  ValueId value;
  bool positive;
  UseId occurrence;
};
struct Equation {
  ValueId input;
  OpId owner;
  bool positive;
  UseId occurrence;
};
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

void ProjectSelectionLogicals(const ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, std::span<SelectionRegion> regions, TreeCheckBudget& budget) {
  if (regions.empty()) return;
  const auto roots = EvaluationRoots(graph, evaluation, budget);
  std::vector<std::optional<Equation>> equations;
  std::vector<std::optional<Truth>> truths;
  std::vector<std::optional<ValueId>> dependents, next;
  std::vector<ValueId> queue;
  std::vector<OpId> path;
  std::vector<std::optional<bool>> pure_blocks;
  budget.Array(equations, graph.values.size()); budget.Array(truths, graph.values.size());
  budget.Array(dependents, graph.values.size()); budget.Array(next, graph.values.size());
  budget.Array(queue, graph.values.size()); budget.Array(path, graph.ops.size());
  budget.Array(pure_blocks, graph.blocks.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto& op = graph.ops[i];
    if (op.life != ValueLife::kLive || !op.result || op.use_count != 1 ||
        evaluation.operations[i].kind != EvaluationKind::kInline ||
        op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
        op.exceptional != ExceptionalCompletion::kImpossible ||
        !std::holds_alternative<ValueBranch>(graph.ops[roots[i].value].operation)) continue;
    const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value].operand);
    const auto* input = std::get_if<ValueId>(&operand);
    if (!input || !graph.values[input->value].domain.IsLanguage()) continue;
    bool positive = true;
    if (const auto* unary = std::get_if<ValueUnary>(&op.operation)) {
      const auto polarity = TruthPolarity(graph, unary->op, operand);
      if (!polarity) continue;
      positive = *polarity;
    } else if (!std::holds_alternative<ValueCopy>(op.operation)) continue;
    equations[op.result->value] = Equation{*input, roots[i], positive, op.first_use};
  }
  std::size_t pending = 0;
  for (std::size_t i = 0; i < equations.size(); ++i) {
    budget.Step(); if (!equations[i]) continue;
    const auto& equation = *equations[i]; const auto& input = equations[equation.input.value];
    if (input && input->owner == equation.owner) {
      next[i] = dependents[equation.input.value]; dependents[equation.input.value] = ValueId{static_cast<std::uint32_t>(i)};
    } else {
      truths[i] = Truth{equation.input, equation.positive, equation.occurrence}; queue[pending++] = {static_cast<std::uint32_t>(i)};
    }
  }
  for (std::size_t i = 0; i < pending; ++i) {
    budget.Step(); const auto value = queue[i];
    for (auto child = dependents[value.value]; child; child = next[child->value]) {
      budget.Step(); const auto& equation = *equations[child->value];
      truths[child->value] = Truth{truths[value.value]->value, truths[value.value]->positive == equation.positive,
          truths[value.value]->occurrence};
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
    for (const auto block : region.condition_blocks) {
      budget.Step(); if (!pure(control.nodes[block.value].block)) closed = false;
    }
    if (!closed) continue;
    std::optional<Truth> condition;
    std::size_t count = 0;
    const auto scalar = [&](OpId branch, bool on_taken) {
      budget.Step(); const auto& op = graph.ops[branch.value];
      if (op.use_count != 1) return false;
      const auto operand = ResolveEvaluationOperand(evaluation, graph.uses[op.first_use.value].operand);
      const auto* value = std::get_if<ValueId>(&operand);
      if (!value || !BooleanEvaluation(graph, operand) || !equations[value->value] ||
          equations[value->value]->owner != branch || !truths[value->value]) return false;
      const auto polarity = TruthPolarity(graph, std::get<ValueBranch>(op.operation).predicate, operand);
      if (!polarity) return false;
      auto truth = *truths[value->value]; truth.positive = (truth.positive == *polarity) == on_taken;
      if (condition && (condition->value != truth.value || condition->positive != truth.positive)) return false;
      if (!condition) condition = truth;
      auto cursor = *value;
      while (equations[cursor.value] && equations[cursor.value]->owner == branch) {
        budget.Step(); const auto* result = std::get_if<OpResult>(&graph.values[cursor.value].origin);
        RequireTree(result && count < path.size(), TreeStructureError::kOwnership);
        path[count++] = result->op; cursor = equations[cursor.value]->input;
      }
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
    const bool truthy_keep = condition->positive == keep_yes;
    SelectionLogical logical{truthy_keep ? TreeLogical::kOr : TreeLogical::kAnd,
        condition->value, condition->occurrence, keep_yes, {}};
    budget.Array(logical.truths, count); std::copy_n(path.begin(), count, logical.truths.begin());
    region.logical = std::move(logical);
  }
}
}  // namespace abcd::source
