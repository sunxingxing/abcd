#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/tree_labels.h"
#include "../../../src/source/loop_syntax.h"
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
    const auto loop = stmt(ForInStmt{object, {2}, {}, ForInBinding::kAssignment, {}});
    const auto callee = expr(SymbolExpr{{1}}), key = expr(SymbolExpr{{2}});
    const auto first_argument = static_cast<std::uint32_t>(tree.arguments.size()); tree.arguments.push_back(key);
    const auto call = expr(CallExpr{callee, {first_argument, 1}, CallReference::kValue});
    const auto action = stmt(ExpressionStmt{call});
    const auto transfer = stmt(stop ? TreeStatement{BreakStmt{loop, {}}} : TreeStatement{ContinueStmt{loop, {}}});
    const auto first = static_cast<std::uint32_t>(tree.children.size());
    tree.children.push_back(action); tree.children.push_back(transfer);
    const auto body = stmt(BlockStmt{scope, first, 2, {}});
    std::get<ForInStmt>(tree.statements[loop.value].statement).body = body;
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
std::variant<RecoveryFlow, TreeStructureError> BuildFlow(const FunctionTree& tree, analysis::SemanticBudget budget = {}) {
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  for (std::uint32_t i=0; i<aliases.size(); ++i) aliases[i]=SymbolId{i};
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()), normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> evaluations(tree.expressions.size(),ExpressionEvaluation::kRequired);
  return BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,evaluations,normal},budget);
}
void CheckEvents(const FunctionTree& tree,unsigned count) {
  const auto result=BuildFlow(tree);
  Check(std::holds_alternative<RecoveryFlow>(result));
  const auto& flow=std::get<RecoveryFlow>(result);
  unsigned acquisitions=0,advances=0,writes=0;
  for(const auto& event:flow.events) {
    acquisitions+=event.kind==FlowEventKind::kEnumerationAcquire;
    advances+=event.kind==FlowEventKind::kEnumerationNext;
    writes+=event.kind==FlowEventKind::kEnumerationBind;
    if(event.kind!=FlowEventKind::kEnumerationNext)continue;
    Check(event.effects&&event.effects->exceptional==analysis::ExceptionalCompletion::kMayThrow);
    std::optional<FlowPoint> success;bool threw=false;
    for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing) {
      const auto& e=flow.edges[*edge];
      if(e.completion==FlowCompletion::kNormal)success=e.to;
      if(e.completion==FlowCompletion::kThrow)threw=true;
    }
    Check(success&&threw);
    const auto& done=flow.events[success->value];
    Check(done.kind==FlowEventKind::kControl&&!done.definition);
    bool exhausted=false,present=false;
    for(auto edge=done.outgoing;edge;edge=flow.edges[*edge].outgoing) {
      const auto& e=flow.edges[*edge];
      if(e.completion==FlowCompletion::kTaken) {exhausted=true;Check(flow.events[e.to.value].kind!=FlowEventKind::kEnumerationBind);}
      if(e.completion==FlowCompletion::kNotTaken) {present=true;Check(flow.events[e.to.value].kind==FlowEventKind::kEnumerationBind);}
    }
    Check(exhausted&&present);
  }
  Check(acquisitions==count&&advances==count&&writes==count);
}
void Labelled() {
  auto tree = Tree(2);
  auto& root = std::get<BlockStmt>(tree.statements[tree.body.value].statement);
  const auto declaration = tree.children[root.first_child];
  const auto outer_id = tree.children[root.first_child + 1];
  const auto inner_id = tree.children[root.first_child + 2];
  const auto returned = tree.children[root.first_child + 3];
  const auto outer = std::get<ForInStmt>(tree.statements[outer_id.value].statement);
  const auto inner = std::get<ForInStmt>(tree.statements[inner_id.value].statement);
  auto& outer_body = std::get<BlockStmt>(tree.statements[outer.body.value].statement);
  const auto& inner_body = std::get<BlockStmt>(tree.statements[inner.body.value].statement);
  const auto first = tree.children[outer_body.first_child];
  const auto transfer = tree.children[outer_body.first_child + 1];
  tree.scopes[inner_body.scope.value].parent = outer_body.scope;
  const auto inner_transfer = tree.children[inner_body.first_child + 1];
  std::get<ContinueStmt>(tree.statements[inner_transfer.value].statement).target = outer_id;
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
  Check(std::get<ForInStmt>(tree.statements[outer_id.value].statement).label.has_value());
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  CheckEvents(tree, 2);
  const auto flow = std::get<RecoveryFlow>(BuildFlow(tree));
  const auto repeat = flow.statement_semantics[outer_id.value]->repeat_point;
  Check(repeat && flow.events[repeat->value].kind == FlowEventKind::kEnumerationNext);
  const auto at = flow.statements[inner_transfer.value];
  bool targets_outer = false;
  for (auto edge = flow.events[at.value].outgoing; edge; edge = flow.edges[*edge].outgoing)
    targets_outer |= flow.edges[*edge].to == *repeat;
  Check(targets_outer);
}

void Budget(unsigned count) {
  const auto tree = Tree(count);
  const auto built = BuildFlow(tree);
  Check(std::holds_alternative<RecoveryFlow>(built));
  const auto& flow = std::get<RecoveryFlow>(built);
  const analysis::SemanticBudget enough{};
  // Test oracle only: locate the exact admitted boundary, including owned
  // syntax, verification scratch and dynamically charged flow buffers.
  std::size_t low = 0, high = enough.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto trial = BuildFlow(tree, {mid, enough.work});
    if (std::holds_alternative<RecoveryFlow>(trial)) high = mid;
    else { Check(std::get<TreeStructureError>(trial) == TreeStructureError::kResourceLimit); low = mid + 1; }
  }
  Check(low > 0);
  Check(std::holds_alternative<RecoveryFlow>(BuildFlow(tree, {low, enough.work})));
  Check(std::get<TreeStructureError>(BuildFlow(tree, {low - 1, enough.work})) == TreeStructureError::kResourceLimit);
  std::cout << "tree scale " << count << " work " << flow.work << " byte_boundary " << low << '\n';
  Check(flow.work > 0);
  Check(std::holds_alternative<RecoveryFlow>(BuildFlow(tree, {enough.bytes, flow.work})));
  Check(std::get<TreeStructureError>(BuildFlow(tree, {enough.bytes, flow.work - 1})) == TreeStructureError::kWorkLimit);
}

}
int main(int argc,char** argv) {
  const auto tree=Tree(); CheckEvents(tree,1); Labelled();
  auto invalid=tree;
  for(auto& statement:invalid.statements) if(auto* loop=std::get_if<ForInStmt>(&statement.statement))loop->binding=ForInBinding::kPerIterationLet;
  Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid))==TreeStructureError::kUnsupportedSyntax);
  invalid=tree;
  for(auto& statement:invalid.statements)if(auto* jump=std::get_if<ContinueStmt>(&statement.statement))jump->target=tree.body;
  Check(std::get<TreeStructureError>(VerifyTreeStructure(invalid))==TreeStructureError::kTarget);
  for(unsigned count:{200U,201U,400U,800U}) { CheckEvents(Tree(count),count); Budget(count); }
  for(bool stop:{false,true}) {
    const auto current=Tree(1,stop);std::ostringstream text;
    const auto result=PrintFunctionTree(current,text);
    if(const auto* error=std::get_if<TreePrintFailure>(&result))std::cerr<<"print "<<unsigned(error->kind)<<'\n';
    Check(std::holds_alternative<PrintedFunction>(result));
    Check(text.str().find("for (")!=std::string::npos);
    if(argc>1) {std::ofstream output(std::string(argv[1])+(stop?"-break.mjs":"-continue.mjs"));output<<"export default "<<text.str()<<";\n";}
  }
}
