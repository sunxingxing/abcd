#include "abcd/source/tree_printer.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../tools/tree_position_json.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value) {if (!value) std::abort();}
struct Syntax {
  FunctionTree tree{};
  StmtId root;
  ExprId call;
  Syntax(unsigned guards, bool formed, bool computed, bool chained, bool reverse, bool value_call=false) {
    tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}}; tree.names = {u"value", u"key", u"source", u"field"};
    for (std::uint32_t i = 0; i < 3; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    auto member = E(SymbolExpr{{0}});
    if(!value_call) member = E(MemberExpr{member, NameId{3}});
    if (chained) member = E(MemberExpr{member, NameId{3}});
    if (computed) member = E(MemberExpr{member, ComputedKey{E(SymbolExpr{{1}})}});
    const auto argument = E(CallExpr{E(SymbolExpr{{1}}), {0, 0}, CallReference::kValue});
    tree.arguments.push_back(argument); call = E(CallExpr{member, {0, 1}, value_call ? CallReference::kValue : CallReference::kMember});
    tree.statements.push_back({BlockStmt{{1}, 0, 0}, {0}});
    auto body = S(ExpressionStmt{call});
    for (unsigned i = guards; i > 0; --i) {
      const auto absent = E(BinaryExpr{analysis::BinaryOperator::kStrictEqual,
        E(SymbolExpr{{0}}), E(TreeLiteral{UndefinedLiteral{}})});
      body = S(IfStmt{absent, B({}), B({body})});
      const auto first = formed && i == 1 ? E(AssignmentExpr{SymbolTarget{{0}},
        E(CallExpr{E(SymbolExpr{{2}}), {0, 0}, CallReference::kValue})}) : E(SymbolExpr{{0}});
      const auto nil = E(BinaryExpr{analysis::BinaryOperator::kStrictEqual, first, E(TreeLiteral{NullLiteral{}})});
      body = S(IfStmt{nil, B({}), B({body})});
    }
    root = body; tree.children.push_back(root);
    std::get<BlockStmt>(tree.statements[0].statement).first_child = static_cast<std::uint32_t>(tree.children.size()-1);
    std::get<BlockStmt>(tree.statements[0].statement).child_count = 1;
    std::vector<std::pair<StmtId, ScopeId>> queue{{tree.body, {0}}};
    for (std::size_t at = 0; at < queue.size(); ++at) {
      const auto [id, scope] = queue[at]; const auto& node = tree.statements[id.value].statement;
      if (const auto* block = std::get_if<BlockStmt>(&node)) {
        tree.scopes[block->scope.value].parent = scope;
        for (std::uint32_t i=0; i<block->child_count; ++i) queue.emplace_back(tree.children[block->first_child+i],block->scope);
      } else if (const auto* branch = std::get_if<IfStmt>(&node)) {
        queue.emplace_back(branch->consequent, scope); if(branch->alternate) queue.emplace_back(*branch->alternate,scope);
      }
    }
    if (reverse) {
      const auto map = [&](StmtId id) {return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)};};
      std::reverse(tree.statements.begin(),tree.statements.end());
      for(auto& node:tree.statements) if(auto* branch=std::get_if<IfStmt>(&node.statement)) {
        branch->consequent=map(branch->consequent);if(branch->alternate) branch->alternate=map(*branch->alternate);
      }
      for(auto& child:tree.children) child=map(child);tree.body=map(tree.body);root=map(root);
    }
  }
  ExprId E(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(expression),{0}});return id;
  }
  StmtId S(TreeStatement statement) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(statement),{0}});return id;
  }
  StmtId B(std::initializer_list<StmtId> items) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),items);
    return S(BlockStmt{scope,first,static_cast<std::uint32_t>(items.size())});
  }
};
}
int main(int argc, char**) {
  std::array<std::uint64_t,32> baseline{};
  for (const unsigned guards : {1u,200u,201u,402u}) for(const bool formed:{false,true})
    for(const bool computed:{false,true}) for(const bool chained:{false,true}) for(const bool reverse:{false,true}) for(const bool value:{false,true}) {
      if(value && (computed || chained)) continue;
      Syntax syntax(guards,formed,computed,chained,reverse,value);std::ostringstream source;
      const auto result=PrintFunctionTree(syntax.tree,source);
      if(const auto* error=std::get_if<TreePrintError>(&result)) std::cerr<<"print error="<<unsigned(*error)<<'\n';
      Check(std::holds_alternative<PrintedFunction>(result));
      if(source.str().find("?.")==std::string::npos) {std::cerr<<"missing native optional statement\n";std::abort();}
      const auto& positions=std::get<PrintedFunction>(result);Check(positions.expressions[syntax.call.value].begin<positions.expressions[syntax.call.value].end);
      const auto planned=PlanNullishSyntax(syntax.tree);Check(std::holds_alternative<NullishSyntax>(planned));
      const auto& plan=std::get<NullishSyntax>(planned);Check(bool(plan.statement_projections[syntax.root.value]));
      const auto base=plan.Base(syntax.tree,NullishRoot{syntax.root});
      const GeneratedRange boundary{positions.expressions[base.value].end,positions.expressions[base.value].end};
      for(std::size_t i=0;i<plan.statement_expressions_by.size();++i) if(plan.statement_expressions_by[i]) Check(positions.expressions[i]==boundary);
      for(std::size_t i=0;i<plan.statement_omitted_by.size();++i) if(plan.statement_omitted_by[i]) Check(positions.statements[i]==boundary);
      const auto shape=(formed?16u:0u)+(computed?8u:0u)+(chained?4u:0u)+(reverse?2u:0u)+(value?1u:0u);
      if(guards==201) baseline[shape]=plan.work;if(guards==402) Check(plan.work<3*baseline[shape]);
      std::ostringstream mapping;tools::WriteTreePositions(mapping,syntax.tree,positions);Check(bool(mapping));
      if(argc>1) std::cout<<nlohmann::json{{"guards",guards},{"formed",formed},{"computed",computed},{"chained",chained},{"reverse",reverse},{"value",value},{"source",source.str()}}<<'\n';
    }
  // A void observation cannot erase abrupt completion, debugger, a second
  // effect, or guards that would consume a reference after an opaque effect.
  for(unsigned form=0;form<4;++form) {
    Syntax syntax(1,true,false,false,false);
    const StmtId leaf{1};const auto original=syntax.tree.statements[leaf.value];
    const auto parent=std::find_if(syntax.tree.statements.begin(),syntax.tree.statements.end(),[&](const auto& node) {
      const auto* block=std::get_if<BlockStmt>(&node.statement);
      return block && block->child_count==1 && syntax.tree.children[block->first_child]==leaf;
    });Check(parent!=syntax.tree.statements.end());
    const auto parent_scope=std::get<BlockStmt>(parent->statement).scope;
    const ScopeId scope{static_cast<std::uint32_t>(syntax.tree.scopes.size())};syntax.tree.scopes.push_back({ScopeKind::kBlock,parent_scope});
    const auto kept=syntax.S(original.statement);
    const auto extra=form==0 ? syntax.S(DebuggerStmt{}) : form==1 ? syntax.S(ReturnStmt{}) :
      form==2 ? syntax.S(ExpressionStmt{syntax.E(MemberExpr{syntax.E(SymbolExpr{{0}}),NameId{3}})}) :
      syntax.S(IfStmt{syntax.E(BinaryExpr{analysis::BinaryOperator::kStrictEqual,syntax.E(SymbolExpr{{0}}),syntax.E(TreeLiteral{NullLiteral{}})}),
        syntax.B({syntax.S(ExpressionStmt{syntax.E(MemberExpr{syntax.E(SymbolExpr{{0}}),NameId{3}})})}),syntax.B({})});
    if(form==3) {
      const auto& branch=std::get<IfStmt>(syntax.tree.statements[extra.value].statement);
      syntax.tree.scopes[std::get<BlockStmt>(syntax.tree.statements[branch.consequent.value].statement).scope.value].parent=scope;
      syntax.tree.scopes[std::get<BlockStmt>(syntax.tree.statements[branch.alternate->value].statement).scope.value].parent=scope;
    }
    const auto first=static_cast<std::uint32_t>(syntax.tree.children.size());
    syntax.tree.children.push_back(kept);syntax.tree.children.push_back(extra);syntax.tree.statements[leaf.value].statement=BlockStmt{scope,first,2};
    const auto planned=PlanNullishSyntax(syntax.tree);Check(std::holds_alternative<NullishSyntax>(planned));
    Check(!std::get<NullishSyntax>(planned).statement_projections[syntax.root.value]);
    std::ostringstream text;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(syntax.tree,text)));
    Check(text.str().find("if (")!=std::string::npos);
  }
  // A missing nil domain and a different binding are semantic counterexamples,
  // regardless of the guard's position or the common statement representation.
  for (const bool foreign : {false, true}) {
    Syntax syntax(1, true, false, false, false);
    const auto guard = std::find_if(syntax.tree.expressions.begin(), syntax.tree.expressions.end(), [&](const auto& node) {
      const auto* comparison = std::get_if<BinaryExpr>(&node.expression);
      if (!comparison) return false;
      const auto* literal = std::get_if<TreeLiteral>(&syntax.tree.expressions[comparison->right.value].expression);
      return literal && std::holds_alternative<UndefinedLiteral>(*literal);
    });
    Check(guard != syntax.tree.expressions.end());
    const auto& comparison = std::get<BinaryExpr>(guard->expression);
    if (foreign) syntax.tree.expressions[comparison.left.value].expression = SymbolExpr{{1}};
    else syntax.tree.expressions[comparison.right.value].expression = TreeLiteral{BooleanLiteral{false}};
    const auto planned = PlanNullishSyntax(syntax.tree);
    Check(std::holds_alternative<NullishSyntax>(planned));
    Check(!std::get<NullishSyntax>(planned).statement_projections[syntax.root.value]);
    std::ostringstream text;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(syntax.tree, text)));
    Check(text.str().find("if (") != std::string::npos);
  }
  Syntax bounded(201, true, true, true, true);
  const auto full = std::get<NullishSyntax>(PlanNullishSyntax(bounded.tree));
  analysis::SemanticBudget memory;
  memory.bytes = full.allocated_bytes - 1;
  Check(std::get<TreeStructureError>(PlanNullishSyntax(bounded.tree, memory)) == TreeStructureError::kResourceLimit);
  analysis::SemanticBudget work;
  work.work = full.work - 1;
  Check(std::get<TreeStructureError>(PlanNullishSyntax(bounded.tree, work)) == TreeStructureError::kWorkLimit);
}
