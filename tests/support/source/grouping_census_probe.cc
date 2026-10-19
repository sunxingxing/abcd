#include "../../../src/source/grouping_plan.h"
#include <nlohmann/json.hpp>
#include <iostream>
using namespace abcd;using namespace abcd::source;
using J=nlohmann::json;
std::optional<analysis::BinaryOperator> Binary(const std::string& s){
 using B=analysis::BinaryOperator;
 static const std::pair<std::string_view,B> names[]={{"PlusToken",B::kAdd},{"MinusToken",B::kSubtract},{"AsteriskToken",B::kMultiply},{"SlashToken",B::kDivide},{"AsteriskAsteriskToken",B::kExponent},{"EqualsEqualsToken",B::kEqual},{"EqualsEqualsEqualsToken",B::kStrictEqual},{"ExclamationEqualsToken",B::kNotEqual},{"ExclamationEqualsEqualsToken",B::kStrictNotEqual},{"LessThanToken",B::kLess},{"GreaterThanToken",B::kGreater},{"LessThanEqualsToken",B::kLessEqual},{"GreaterThanEqualsToken",B::kGreaterEqual},{"InKeyword",B::kIn},{"InstanceOfKeyword",B::kInstanceOf}};
 for(auto [name,b]:names)if(s==name)return b;return {};
}
int main(){std::string line;while(std::getline(std::cin,line)){
 auto j=J::parse(line);const auto child=j.at("child").get<std::string>(),parent=j.at("parent").get<std::string>(),op=j.value("operator",std::string{});
 TreeExpression e=SymbolExpr{{0}};bool known=true;
 if(child=="VoidExpression")e=TreeLiteral{UndefinedLiteral{}};
 else if(child=="ObjectLiteralExpression")e=EmptyObjectExpr{};
 else if(child=="ConditionalExpression")e=ConditionalExpr{{0},{1},{2}};
 else if(child=="AwaitExpression")e=AwaitExpr{{0}};
 else if(child=="ArrowFunction"||child=="FunctionExpression")e=FunctionExpr{{0},{0,0}};
 else if(child=="BinaryExpression"){
  if(op=="EqualsToken")e=AssignmentExpr{SymbolTarget{{0}},{1}};
  else if(op=="CommaToken")e=SequenceExpr{{0},{1}};
  else if(auto b=Binary(op))e=BinaryExpr{*b,{0},{1}};
  else if(op=="BarBarToken"||op=="AmpersandAmpersandToken"||op=="QuestionQuestionToken")e=LogicalExpr{op=="BarBarToken"?TreeLogical::kOr:op=="AmpersandAmpersandToken"?TreeLogical::kAnd:TreeLogical::kNullish,{0},{1}};
  else known=false;
 }else if(child!="Identifier")known=false;
 GroupingContext c;c.statement_start=j.value("statement_start",false);
 if(parent.starts_with("AssignmentValue/"))c.operand={ExpressionGrammar::kAssignmentValue};
 else if(parent.starts_with("Expression/")||parent=="StatementStart")c.operand={ExpressionGrammar::kExpression};
 else if(parent=="MemberBase")c.operand={PostfixContext::kMemberBase};
 else if(parent=="CallCallee")c.operand={PostfixContext::kCallCallee};
 else if(parent=="Constructor")c.operand={PostfixContext::kConstructor};
 else if(parent=="ConditionalTest")c.operand={ExpressionGrammar::kConditionalTest};
 else if(parent=="ArrowBody"){c.operand={ExpressionGrammar::kAssignmentValue};c.arrow_body=true;}
 else if(parent.starts_with("Binary/")){auto end=parent.find('/',7);if(auto b=Binary(parent.substr(7,end-7)))c.operand={*b,parent.ends_with("/right")?OperandSide::kRight:OperandSide::kLeft};}
 else if(parent.starts_with("Unary/"))c.operand={ExpressionPrecedence::kUnary};
 const auto decision=!known?GroupingDecision{GroupingReason::kUnknown}:child=="VoidExpression"?ReadableLiteralEnvelope(std::get<TreeLiteral>(e),c.operand):ReadableGrouping(e,c,ExpressionHeadFacts(e));
 static constexpr std::string_view reasons[]={"kNone","kPrecedence","kStatementStart","kDirective","kNoIn","kArrowBody","kChainBoundary","kUnknown"};
 std::cout<<reasons[static_cast<unsigned>(decision.reason)]<<'\n';
}}
