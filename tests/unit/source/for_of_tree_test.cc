#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_labels.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
FunctionTree Tree(unsigned count = 1, bool stop = false) {
  FunctionTree tree{};
  tree.owner = {0}; tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.origins = {OriginalSite{{0}, {0}}};
  tree.names = {u"object", u"visit", u"key"};
  tree.symbols = {{{0}, {0}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}},
      {{0}, {1}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}},
      {{1}, {2}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kMetadata, {}}};
  tree.parameters = {{{0}, {}, false}, {{1}, {}, false}};
  const auto expr = [&](TreeExpression value) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())}; tree.expressions.push_back({std::move(value), {0}}); return id;
  };
  const auto stmt = [&](TreeStatement value) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())}; tree.statements.push_back({std::move(value), {0}}); return id;
  };
  stmt(BlockStmt{{1}, 0, 0, {}});
  std::vector<StmtId> roots;
  roots.push_back(stmt(DeclarationStmt{{2}, expr(TreeLiteral{IntegerLiteral{17}})}));
  for (unsigned n = 0; n < count; ++n) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
    tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
    const auto object = expr(SymbolExpr{{0}});
    const auto loop = stmt(ForOfStmt{object, {2}, {}, ForOfBinding::kAssignment, {}});
    const auto callee = expr(SymbolExpr{{1}}), key = expr(SymbolExpr{{2}});
    const auto first_argument = static_cast<std::uint32_t>(tree.arguments.size()); tree.arguments.push_back(key);
    const auto call = expr(CallExpr{callee, {first_argument, 1}, CallReference::kValue});
    const auto action = stmt(ExpressionStmt{call});
    const auto transfer = stmt(stop ? TreeStatement{BreakStmt{loop, {}}} : TreeStatement{ContinueStmt{loop, {}}});
    const auto first = static_cast<std::uint32_t>(tree.children.size());
    tree.children.push_back(action); tree.children.push_back(transfer);
    const auto body = stmt(BlockStmt{scope, first, 2, {}});
    std::get<ForOfStmt>(tree.statements[loop.value].statement).body = body;
    roots.push_back(loop);
  }
  roots.push_back(stmt(ReturnStmt{expr(SymbolExpr{{2}})}));
  auto& body = std::get<BlockStmt>(tree.statements[0].statement);
  body.first_child = static_cast<std::uint32_t>(tree.children.size()); body.child_count = static_cast<std::uint32_t>(roots.size());
  tree.children.insert(tree.children.end(), roots.begin(), roots.end());
  Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  return tree;
}
FunctionTree Labelled(bool stop = false) {
  auto tree = Tree(2);
  auto& root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  const auto declaration = tree.children[root.first_child];
  const auto outer_id = tree.children[root.first_child + 1];
  const auto inner_id = tree.children[root.first_child + 2];
  const auto returned = tree.children[root.first_child + 3];
  const auto outer = std::get<ForOfStmt>(tree.statements[outer_id.value].statement);
  const auto inner = std::get<ForOfStmt>(tree.statements[inner_id.value].statement);
  auto& outer_body = std::get<BlockStmt>(tree.statements[outer.body.value].statement);
  const auto& inner_body = std::get<BlockStmt>(tree.statements[inner.body.value].statement);
  const auto first = tree.children[outer_body.first_child];
  const auto transfer = tree.children[outer_body.first_child + 1];
  tree.scopes[inner_body.scope.value].parent = outer_body.scope;
  const auto inner_transfer = tree.children[inner_body.first_child + 1];
  tree.statements[inner_transfer.value].statement = stop ? TreeStatement{BreakStmt{outer_id,{}}} : TreeStatement{ContinueStmt{outer_id,{}}};
  outer_body.first_child = static_cast<std::uint32_t>(tree.children.size());
  outer_body.child_count = 3;
  tree.children.insert(tree.children.end(), {first, inner_id, transfer});
  root.first_child = static_cast<std::uint32_t>(tree.children.size()); root.child_count = 3;
  tree.children.insert(tree.children.end(), {declaration, outer_id, returned});
  // Repack the actual children so abandoned source arenas cannot hide
  // unowned occurrences from the independent structural verifier.
  std::vector<StmtId> children;
  for (auto& statement : tree.statements) if (auto* block = std::get_if<BlockStmt>(&statement.statement)) {
    const auto start = static_cast<std::uint32_t>(children.size());
    children.insert(children.end(), tree.children.begin() + block->first_child,
        tree.children.begin() + block->first_child + block->child_count);
    block->first_child = start;
  }
  tree.children = std::move(children);
  Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
  Check(std::get<ForOfStmt>(tree.statements[outer_id.value].statement).label.has_value());
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  return tree;
}

FunctionTree Nested(unsigned count) {
  auto tree=Tree(count);
  auto& root=std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  std::vector<StmtId> loops;
  for(unsigned i=0;i<count;++i)loops.push_back(tree.children[root.first_child+i+1]);
  const auto declaration=tree.children[root.first_child],returned=tree.children[root.first_child+root.child_count-1];
  for(unsigned i=0;i+1<count;++i) {
    auto& body=std::get<BlockStmt>(tree.statements[std::get<ForOfStmt>(tree.statements[loops[i].value].statement).body.value].statement);
    const auto action=tree.children[body.first_child],transfer=tree.children[body.first_child+1];
    const auto inner=std::get<ForOfStmt>(tree.statements[loops[i+1].value].statement).body;
    tree.scopes[std::get<BlockStmt>(tree.statements[inner.value].statement).scope.value].parent=body.scope;
    body.first_child=static_cast<std::uint32_t>(tree.children.size());body.child_count=3;
    tree.children.insert(tree.children.end(),{action,loops[i+1],transfer});
  }
  root.first_child=static_cast<std::uint32_t>(tree.children.size());root.child_count=3;
  tree.children.insert(tree.children.end(),{declaration,loops[0],returned});
  std::vector<StmtId> children;
  for(auto& statement:tree.statements)if(auto* block=std::get_if<BlockStmt>(&statement.statement)) {
    const auto first=static_cast<std::uint32_t>(children.size());
    children.insert(children.end(),tree.children.begin()+block->first_child,tree.children.begin()+block->first_child+block->child_count);
    block->first_child=first;
  }
  tree.children=std::move(children);
  Check(std::holds_alternative<TreeStorage>(AssignTreeLabels(tree)));
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  return tree;
}
FunctionTree SequenceRhs() {
  auto tree=Tree();
  for(auto& statement:tree.statements)if(auto* loop=std::get_if<ForOfStmt>(&statement.statement)) {
    const auto id=loop->iterable;
    const ExprId left{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({TreeLiteral{IntegerLiteral{0}},{0}});
    const ExprId right{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back(tree.expressions[id.value]);
    tree.expressions[id.value].expression=SequenceExpr{left,right};
  }
  return tree;
}

void PrintBudget(const FunctionTree& tree) {
  const auto print=[&](analysis::SemanticBudget budget) {
    std::ostringstream output;return PrintFunctionTree(tree,output,{},budget);
  };
  const analysis::SemanticBudget enough{};
  const auto baseline=print(enough);Check(std::holds_alternative<PrintedFunction>(baseline));
  const auto work=std::get<PrintedFunction>(baseline).work;
  Check(std::holds_alternative<PrintedFunction>(print({enough.bytes,work})));
  Check(std::get<TreePrintFailure>(print({enough.bytes,work-1})).kind==TreePrintError::kWorkLimit);
  std::size_t low=0,high=enough.bytes;
  while(low<high) {
    const auto mid=low+(high-low)/2;const auto result=print({mid,enough.work});
    if(std::holds_alternative<PrintedFunction>(result))high=mid;
    else {Check(std::get<TreePrintFailure>(result).kind==TreePrintError::kResourceLimit);low=mid+1;}
  }
  Check(low>0&&std::holds_alternative<PrintedFunction>(print({low,enough.work})));
  Check(std::get<TreePrintFailure>(print({low-1,enough.work})).kind==TreePrintError::kResourceLimit);
}

void Budget(unsigned count) {
  const auto tree = Tree(count);
  const analysis::SemanticBudget enough{};
  // Test oracle only: locate the exact admitted boundary, including owned
  // syntax and verification scratch. No protocol/recovery admission.
  std::size_t low = 0, high = enough.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto trial = VerifyTreeStructure(tree, {mid, enough.work});
    if (std::holds_alternative<TreeStructure>(trial)) high = mid;
    else { Check(std::get<TreeStructureError>(trial) == TreeStructureError::kResourceLimit); low = mid + 1; }
  }
  Check(low > 0);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree, {low, enough.work})));
  Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, {low - 1, enough.work})) == TreeStructureError::kResourceLimit);
  std::cout << "tree scale " << count << " work " << std::get<TreeStructure>(VerifyTreeStructure(tree)).work << " byte_boundary " << low << '\n';
  Check(std::get<TreeStructure>(VerifyTreeStructure(tree)).work > 0);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree, {enough.bytes, std::get<TreeStructure>(VerifyTreeStructure(tree)).work})));
  Check(std::get<TreeStructureError>(VerifyTreeStructure(tree, {enough.bytes, std::get<TreeStructure>(VerifyTreeStructure(tree)).work - 1})) == TreeStructureError::kWorkLimit);
}

}
int main(int argc,char** argv) {
  const auto tree=Tree(); Labelled(); Labelled(true);
  auto missing=tree;
  for(auto& statement:missing.statements)if(auto* loop=std::get_if<ForOfStmt>(&statement.statement))loop->body={static_cast<std::uint32_t>(missing.statements.size())};
  Check(std::get<TreeStructureError>(VerifyTreeStructure(missing))==TreeStructureError::kScope);

  auto label=Labelled();
  Check(!label.labels.empty());label.labels[0]=label.body;
  Check(std::get<TreeStructureError>(VerifyTreeStructure(label))==TreeStructureError::kTarget);
  auto invalid=tree;
  for(auto& statement:invalid.statements) if(auto* loop=std::get_if<ForOfStmt>(&statement.statement))loop->binding=ForOfBinding::kPerIterationLet;
  Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid))==TreeStructureError::kUnsupportedSyntax);
  std::ostringstream refused;
  Check(std::get<TreePrintFailure>(PrintFunctionTree(invalid,refused)).kind==TreePrintError::kUnsupportedContext);
  Check(refused.str().empty());
  invalid=tree;
  for(auto& statement:invalid.statements)if(auto* jump=std::get_if<ContinueStmt>(&statement.statement))jump->target=tree.body;
  Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid))==TreeStructureError::kTarget);
  for(unsigned count:{200U,201U,400U,800U}) { Budget(count); Nested(count); PrintBudget(Tree(count)); }
  for(bool stop:{false,true}) {
    const auto current=Tree(1,stop);std::ostringstream text;
    const auto result=PrintFunctionTree(current,text);
    if(const auto* error=std::get_if<TreePrintFailure>(&result))std::cerr<<"print "<<unsigned(error->kind)<<'\n';
    Check(std::holds_alternative<PrintedFunction>(result));
    Check(text.str().find(" of ")!=std::string::npos);
    Check(text.str().find("for (let ")==std::string::npos);
    const auto repeated=PrintFunctionTree(current,std::cout,{0});
    Check(std::get<TreePrintFailure>(repeated).kind==TreePrintError::kOutputLimit);
    if(argc>1) {std::ofstream output(std::string(argv[1])+(stop?"-break.mjs":"-continue.mjs"));output<<"export default "<<text.str()<<";\n";}
  }
  for(bool stop:{false,true}) {
    const auto nested=Labelled(stop);std::ostringstream text;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(nested,text)));
    Check(text.str().find(stop?"break ":"continue ")!=std::string::npos);
    if(argc>1){std::ofstream output(std::string(argv[1])+(stop?"-nested-break.mjs":"-nested-continue.mjs"));output<<"export default "<<text.str()<<";\n";}
  }

  {
    const auto sequence=SequenceRhs();std::ostringstream text;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(sequence,text)));
    Check(text.str().find(" of (0, ")!=std::string::npos);
    if(argc>1){std::ofstream output(std::string(argv[1])+"-sequence.mjs");output<<"export default "<<text.str()<<";\n";}
  }

  {
    auto named=Tree();named.names[2]=u"async";std::ostringstream text;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(named,text)));
    if(argc>1){std::ofstream output(std::string(argv[1])+"-async-name.mjs");output<<"export default "<<text.str()<<";\n";}
  }

}
