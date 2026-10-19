#include "grouping_plan.h"
#include "expression_children.h"
#include <limits>

namespace abcd::source {
namespace {
std::vector<ExpressionSyntaxFacts> Facts(const FunctionTree& tree, TreeCheckBudget& budget) {
  struct Visit { ExprId id; bool finish; };
  std::vector<ExpressionSyntaxFacts> result;
  std::vector<std::uint8_t> state;
  std::vector<Visit> stack;
  budget.Array(result, tree.expressions.size());
  budget.Array(state, tree.expressions.size());
  if (tree.expressions.size() > std::numeric_limits<std::size_t>::max() / 2)
    throw TreeStructureError::kResourceLimit;
  budget.Array(stack, tree.expressions.size() * 2);
  // Postorder over the checked forest; no dependence on arena allocation order
  // and no repeated scans of a left spine. Every buffer is prepaid.
  for (std::size_t root = 0; root < tree.expressions.size(); ++root) {
    budget.Step(); if (state[root] == 2) continue;
    std::size_t depth = 1;
    stack[0] = {ExprId{static_cast<std::uint32_t>(root)}, false};
    while (depth) {
      budget.Step(); const auto visit = stack[--depth]; const auto id = visit.id.value;
      RequireTree(id < result.size(), TreeStructureError::kOwnership);
      const auto& node = tree.expressions[id].expression;
      if (!visit.finish) {
        if (state[id] == 2) continue;
        RequireTree(state[id] == 0, TreeStructureError::kOwnership);
        state[id] = 1;
        RequireTree(depth < stack.size(), TreeStructureError::kOwnership);
        stack[depth++] = {visit.id, true};
        VisitExpressionChildren(tree, node, [&](ExprId child) {
          budget.Step(); RequireTree(depth < stack.size(), TreeStructureError::kOwnership);
          stack[depth++] = {child, false};
        });
        continue;
      }
      auto& fact = result[id]; const auto* object = std::get_if<ObjectExpr>(&node);
      fact.object_literal = std::holds_alternative<EmptyObjectExpr>(node) ||
          (object && object->kind == AggregateKind::kObject);
      fact.statement_hazard = fact.object_literal || std::holds_alternative<FunctionExpr>(node) ||
          std::holds_alternative<ClassExpr>(node);
      if (const auto* literal = std::get_if<TreeLiteral>(&node))
        fact.string_literal = std::holds_alternative<TextId>(*literal);
      if (const auto* binary = std::get_if<BinaryExpr>(&node))
        fact.contains_in = binary->operation == analysis::BinaryOperator::kIn;
      VisitExpressionChildren(tree, node, [&](ExprId child) {
        budget.Step(); fact.contains_in |= result[child.value].contains_in;
      });
      std::optional<ExprId> left;
      std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T,BinaryExpr> || std::is_same_v<T,LogicalExpr> || std::is_same_v<T,SequenceExpr>) left=n.left;
        else if constexpr (std::is_same_v<T,ConditionalExpr>) left=n.condition;
        else if constexpr (std::is_same_v<T,MemberExpr>) left=n.base;
        else if constexpr (std::is_same_v<T,CallExpr>) { if(n.reference!=CallReference::kValue) left=n.callee; }
        else if constexpr (std::is_same_v<T,AssignmentExpr>) {
          if(const auto* member=std::get_if<MemberTarget>(&n.target)) left=member->base;
        }
      },node);
      if (left) fact.statement_hazard |= result[left->value].statement_hazard;
      state[id] = 2;
    }
  }
  return result;
}
GroupingDecision Decide(const TreeExpression& expression, ExpressionSyntaxFacts facts, const GroupingContext& context) {
  if(context.chain_boundary) return {GroupingReason::kChainBoundary};
  if(context.directive_position && facts.string_literal) return {GroupingReason::kDirective};
  if(context.statement_start && facts.statement_hazard) return {GroupingReason::kStatementStart};
  if(!context.allow_in && facts.contains_in) return {GroupingReason::kNoIn};
  if(context.arrow_body && (facts.object_literal || std::holds_alternative<SequenceExpr>(expression)))
    return {GroupingReason::kArrowBody};
  if(std::holds_alternative<std::monostate>(context.operand.parent)) return {GroupingReason::kUnknown};
  if(const auto* grammar=std::get_if<ExpressionGrammar>(&context.operand.parent);
      grammar && *grammar==ExpressionGrammar::kUnknown) return {GroupingReason::kUnknown};
  return {ReadableOperandGrouping(expression,context.operand) ? GroupingReason::kPrecedence : GroupingReason::kNone};
}
}
GroupingDecision ReadableGrouping(const TreeExpression& expression, const GroupingContext& context,
    ExpressionSyntaxFacts facts) { return Decide(expression, facts, context); }
GroupingPlan BuildGroupingPlan(const FunctionTree& tree, std::span<const GroupingOccurrence> uses, TreeCheckBudget& budget) {
  GroupingPlan result; result.facts=Facts(tree,budget);budget.Array(result.occurrences,uses.size());
  for(std::size_t i=0;i<uses.size();++i) {
    budget.Step();const auto id=uses[i].expression.value;
    RequireTree(id<result.facts.size(),TreeStructureError::kOwnership);
    result.occurrences[i]=Decide(tree.expressions[id].expression,result.facts[id],uses[i].context);
  }
  return result;
}
bool VerifyGroupingPlan(const FunctionTree& tree,std::span<const GroupingOccurrence> uses,const GroupingPlan& plan,TreeCheckBudget& budget) {
  if(plan.policy!=PrinterPolicy::kReadableV1 || plan.occurrences.size()!=uses.size()) return false;
  const auto facts=Facts(tree,budget);
  if(facts!=plan.facts) return false;
  for(std::size_t i=0;i<uses.size();++i) {
    budget.Step();const auto id=uses[i].expression.value;
    if(id>=facts.size() || plan.occurrences[i]!=Decide(tree.expressions[id].expression,facts[id],uses[i].context)) return false;
  }
  return true;
}
}
