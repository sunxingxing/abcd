#include "../../../src/source/grouping_plan.h"
#include <cstdlib>
#include <iostream>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value) { if(!value) std::abort(); }
ExprId Add(FunctionTree& tree, TreeExpression expression) {
  const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
  tree.expressions.push_back({std::move(expression),{0}});return id;
}
GroupingContext Assignment() {return {{ExpressionGrammar::kAssignmentValue}};}
void Grammar() {
  Check(!ReadableLiteralEnvelope(TreeLiteral{UndefinedLiteral{}},{ExpressionGrammar::kAssignmentValue}).Parenthesized());
  Check(!ReadableLiteralEnvelope(TreeLiteral{UndefinedLiteral{}},{ExpressionGrammar::kExpression}).Parenthesized());
  Check(ReadableLiteralEnvelope(TreeLiteral{UndefinedLiteral{}},{PostfixContext::kMemberBase}).Parenthesized());
  Check(ReadableLiteralEnvelope(TreeLiteral{UndefinedLiteral{}},{}).Parenthesized());
  Check(ReadableLiteralEnvelope(TreeLiteral{IntegerLiteral{-1}},{ExpressionGrammar::kAssignmentValue}).Parenthesized());

  FunctionTree tree;
  const auto a=Add(tree,TreeLiteral{IntegerLiteral{1}});
  const auto b=Add(tree,TreeLiteral{IntegerLiteral{2}});
  const auto object=Add(tree,EmptyObjectExpr{});
  const auto text=Add(tree,TreeLiteral{TextId{0}});
  const auto sequence=Add(tree,SequenceExpr{a,b});
  const auto conditional=Add(tree,ConditionalExpr{a,b,object});
  const auto unary=Add(tree,UnaryExpr{TreeUnary::kNegate,a});
  const auto in=Add(tree,BinaryExpr{analysis::BinaryOperator::kIn,a,b});
  const auto logical=Add(tree,LogicalExpr{TreeLogical::kAnd,a,b});
  const auto sum=Add(tree,BinaryExpr{analysis::BinaryOperator::kAdd,a,b});
  std::vector<GroupingOccurrence> uses{
    {object,Assignment()},{conditional,Assignment()},{sequence,Assignment()},
    {object,{{ExpressionGrammar::kExpression},true}},
    {text,{{ExpressionGrammar::kExpression},true,true}},
    {in,{{ExpressionGrammar::kExpression},false,false,false}},
    {object,{{ExpressionGrammar::kExpression},false,false,true,true}},
    {unary,{{analysis::BinaryOperator::kExponent,OperandSide::kLeft}}},
    {logical,{{TreeLogical::kNullish}}},
    {sum,{{analysis::BinaryOperator::kAdd,OperandSide::kRight}}},
    {sum,{{analysis::BinaryOperator::kAdd,OperandSide::kLeft}}},
    {a,{{ExpressionGrammar::kExpression},false,false,true,false,true}}
  };
  TreeCheckBudget budget({});const auto plan=BuildGroupingPlan(tree,uses,budget);
  for(std::size_t i=0;i<uses.size();++i)
    Check(plan.occurrences[i].Parenthesized()==(i!=0 && i!=1 && i!=10));
  TreeCheckBudget verify({});Check(VerifyGroupingPlan(tree,uses,plan,verify));
  for(std::size_t i=0;i<uses.size();++i) {
    auto mutation=plan;mutation.occurrences[i].reason=plan.occurrences[i].Parenthesized()?GroupingReason::kNone:GroupingReason::kUnknown;
    TreeCheckBudget check({});Check(!VerifyGroupingPlan(tree,uses,mutation,check));
  }
  auto mutation=plan;mutation.facts[object.value].object_literal=false;
  TreeCheckBudget check({});Check(!VerifyGroupingPlan(tree,uses,mutation,check));
}
void Operators() {
  using B=analysis::BinaryOperator; using P=ExpressionPrecedence;
  const ExprId a{0},b{1};
  Check(ReadableOperandGrouping(BinaryExpr{B::kSubtract,a,b},{B::kSubtract,OperandSide::kRight}));
  Check(!ReadableOperandGrouping(BinaryExpr{B::kSubtract,a,b},{B::kSubtract,OperandSide::kLeft}));
  Check(ReadableOperandGrouping(BinaryExpr{B::kExponent,a,b},{B::kExponent,OperandSide::kLeft}));
  Check(!ReadableOperandGrouping(BinaryExpr{B::kExponent,a,b},{B::kExponent,OperandSide::kRight}));
  Check(ReadableOperandGrouping(UnaryExpr{TreeUnary::kPlus,a},{TreeUnary::kPlus}));
  Check(ReadableOperandGrouping(UnaryExpr{TreeUnary::kNegate,a},{TreeUnary::kNegate}));
  Check(ReadableOperandGrouping(UpdateExpr{{0},UpdateDirection::kIncrement,UpdateResult::kPrefix},{TreeUnary::kPlus}));
  Check(ReadableOperandGrouping(UpdateExpr{{0},UpdateDirection::kDecrement,UpdateResult::kPrefix},{TreeUnary::kNegate}));
  Check(!ReadableOperandGrouping(UpdateExpr{{0},UpdateDirection::kIncrement,UpdateResult::kPostfix},{TreeUnary::kPlus}));
  Check(ReadableOperandGrouping(SequenceExpr{a,b},{ExpressionGrammar::kAssignmentValue}));
  Check(!ReadableOperandGrouping(SequenceExpr{a,b},{ExpressionGrammar::kExpression}));
  Check(ReadableOperandGrouping(ConditionalExpr{a,b,a},{ExpressionGrammar::kConditionalTest}));
  Check(!ReadableOperandGrouping(ConditionalExpr{a,b,a},{ExpressionGrammar::kAssignmentValue}));
  Check(ReadableOperandGrouping(TreeLiteral{IntegerLiteral{1}},{PostfixContext::kMemberBase}));
  Check(!ReadableOperandGrouping(TreeLiteral{IntegerLiteral{1}},{PostfixContext::kCallCallee}));
  Check(ReadableOperandGrouping(UpdateExpr{{0},UpdateDirection::kIncrement,UpdateResult::kPostfix},{PostfixContext::kMemberBase}));
  Check(ReadableOperandGrouping(UpdateExpr{{0},UpdateDirection::kIncrement,UpdateResult::kPostfix},{PostfixContext::kCallCallee}));
  Check(ReadableOperandGrouping(AssignmentExpr{SymbolTarget{{0}},a},{PostfixContext::kMemberBase}));
  Check(!ReadableOperandGrouping(AssignmentExpr{SymbolTarget{{0}},a},{ExpressionGrammar::kAssignmentValue}));
  Check(ReadableOperandGrouping(EmptyObjectExpr{},{ExpressionGrammar::kUnknown}));
  Check(StatementIdentifierGrouping(u"let",{{PostfixContext::kMemberBase},true}));
  Check(!StatementIdentifierGrouping(u"let",{{PostfixContext::kMemberBase},false}));
  Check(!StatementIdentifierGrouping(u"other",{{PostfixContext::kMemberBase},true}));
  Check(!StatementIdentifierGrouping(u"let",{{ExpressionGrammar::kAssignmentValue},true}));
  Check(TreeExpressionPrecedence(SequenceExpr{a,b})==P::kSequence);
  for (const auto child : {TreeExpression{FunctionExpr{{0},{0,0}}},TreeExpression{ClassExpr{}}}) {
    const auto facts=ExpressionHeadFacts(child); Check(facts.statement_hazard);
    Check(!ReadableGrouping(child,{{ExpressionGrammar::kAssignmentValue}},facts).Parenthesized());
    Check(ReadableGrouping(child,{{ExpressionGrammar::kExpression},true},facts).Parenthesized());
    Check(ReadableGrouping(child,{{PostfixContext::kMemberBase}},facts).Parenthesized());
    Check(ReadableGrouping(child,{},facts).Parenthesized());
  }
  Check(ReadableGrouping(EmptyObjectExpr{},{{PostfixContext::kMemberBase},true},
      ExpressionHeadFacts(EmptyObjectExpr{})).Parenthesized());
  Check(!ReadableGrouping(EmptyObjectExpr{},{{PostfixContext::kMemberBase}},
      ExpressionHeadFacts(EmptyObjectExpr{})).Parenthesized());
}
void Scale(std::uint32_t count,bool deep) {
  FunctionTree tree;auto last=Add(tree,TreeLiteral{IntegerLiteral{1}});
  std::vector<GroupingOccurrence> uses;
  for(std::uint32_t i=0;i<count;++i) {
    const auto leaf=Add(tree,TreeLiteral{IntegerLiteral{2}});
    last=Add(tree,BinaryExpr{analysis::BinaryOperator::kAdd,deep?last:leaf,leaf});
    uses.push_back({last,Assignment()});
  }
  TreeCheckBudget budget({});auto plan=BuildGroupingPlan(tree,uses,budget);
  TreeCheckBudget verify({});Check(VerifyGroupingPlan(tree,uses,plan,verify));
  analysis::SemanticBudget exact{budget.Bytes(),budget.Work()};
  {TreeCheckBudget b(exact);Check(BuildGroupingPlan(tree,uses,b).occurrences==plan.occurrences);}
  try {auto b=exact;--b.bytes;TreeCheckBudget short_budget(b);(void)BuildGroupingPlan(tree,uses,short_budget);Check(false);}
  catch(TreeStructureError e){Check(e==TreeStructureError::kResourceLimit);}
  try {auto b=exact;--b.work;TreeCheckBudget short_budget(b);(void)BuildGroupingPlan(tree,uses,short_budget);Check(false);}
  catch(TreeStructureError e){Check(e==TreeStructureError::kWorkLimit);}
  std::cout<<count<<(deep?" depth ":" width ")<<budget.Bytes()<<" bytes "<<budget.Work()<<" work\n";
}
}
int main(){Grammar();Operators();for(auto n:{200u,201u,400u,800u}){Scale(n,false);Scale(n,true);}}
