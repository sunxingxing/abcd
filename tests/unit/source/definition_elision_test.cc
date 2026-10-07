#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
struct Fixture {
  FunctionTree tree{};
  Fixture() {
    tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"produce",u"flag",u"state",u"error"};tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<2;++i) {tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
    tree.symbols.push_back({{1},{2},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression node) {ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;}
  StmtId Stmt(TreeStatement node) {StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;}
  ExprId Read(unsigned symbol) {return Expr(SymbolExpr{{symbol}});}
  ExprId Call(unsigned value) {const auto arg=Expr(TreeLiteral{IntegerLiteral{static_cast<std::int32_t>(value)}});const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(arg);return Expr(CallExpr{Read(0),{first,1},CallReference::kValue});}
  ExprId Store(unsigned value) {return Expr(AssignmentExpr{SymbolTarget{{2}},Call(value)});}
  StmtId Block(std::vector<StmtId> body,ScopeId scope) {const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),body.begin(),body.end());return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(body.size())});}
  ScopeId Scope(ScopeKind kind=ScopeKind::kBlock) {const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({kind,ScopeId{1}});return id;}
  void Finish(std::vector<StmtId> body,bool reverse) {
    auto& root=std::get<BlockStmt>(tree.statements[0].statement);root.first_child=tree.children.size();root.child_count=body.size();tree.children.insert(tree.children.end(),body.begin(),body.end());
    for(unsigned i=0;i<tree.statements.size();++i)if(const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement))for(unsigned j=0;j<=block->child_count;++j){const auto anchor=static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},j},{0}});tree.operations.push_back({{anchor},AnchorId{anchor}});}
    if(!reverse)return;
    const auto remap=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
    for(auto& expression:tree.expressions)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,AssignmentExpr>)node.value=remap(node.value);
      else if constexpr(std::is_same_v<T,CallExpr>)node.callee=remap(node.callee);
    },expression.expression);
    for(auto& statement:tree.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=remap(*node.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=remap(node.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=remap(*node.value);}
      else if constexpr(std::is_same_v<T,IfStmt>)node.condition=remap(node.condition);
    },statement.statement);
    for(auto& arg:tree.arguments)arg=remap(arg);std::reverse(tree.expressions.begin(),tree.expressions.end());
  }
};
std::uint64_t Check(unsigned form,unsigned count,bool reverse,bool emit,bool resources=false) {
  Fixture f;auto& tree=f.tree;
  if(resources)tree.identity.contract=SourceContract::kLanguageAndHostV1;
  std::vector<StmtId> body{f.Stmt(DeclarationStmt{{2},form>=5?std::nullopt:std::optional{f.Read(1)}})};
  if(form==6) {const auto arg=f.Read(2);const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(arg);body.push_back(f.Stmt(ExpressionStmt{f.Expr(CallExpr{f.Read(0),{first,1},CallReference::kValue})}));}
  for(unsigned i=0;i<count;++i)body.push_back(f.Stmt(ExpressionStmt{f.Call(0)}));
  const auto first=f.Store(1);body.push_back(f.Stmt(form==4?TreeStatement{ReturnStmt{first}}:TreeStatement{ExpressionStmt{first}}));
  if(form==1) {
    const auto second=f.Stmt(ExpressionStmt{f.Store(2)});
    const auto scope=f.Scope(ScopeKind::kCatch);
    tree.symbols.push_back({scope,{3},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto handler=f.Block({f.Stmt(ReturnStmt{f.Read(2)})},scope);
    body.push_back(f.Stmt(TryStmt{f.Block({second},f.Scope()),CatchClause{{3},handler},{}}));
  } else if(form==2||form==3) {
    const auto second=f.Stmt(ExpressionStmt{f.Store(2)});
    std::optional<StmtId> alternate;
    if(form==3)alternate=f.Block({f.Stmt(ExpressionStmt{f.Store(3)})},f.Scope());
    body.push_back(f.Stmt(IfStmt{f.Read(1),f.Block({second},f.Scope()),alternate}));
  } else if(form!=4)body.push_back(f.Stmt(ExpressionStmt{f.Store(2)}));
  if(form!=4)body.push_back(f.Stmt(ReturnStmt{f.Read(2)}));
  f.Finish(body,reverse);
  const auto verified=VerifyTreeStructure(tree);
  if(const auto* error=std::get_if<TreeStructureError>(&verified))std::cerr<<"form="<<form<<" count="<<count<<" reverse="<<reverse<<" structure="<<static_cast<unsigned>(*error)<<'\n';
  test::Check(std::holds_alternative<TreeStructure>(verified));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(planned));const auto& plan=std::get<BindingInitializers>(planned);
  const auto id=reverse?tree.expressions.size()-1-first.value:first.value;
  test::Check(plan.replacements[id].has_value()==(!resources&&(form==0||form==3||form==4||form==6)));
  // Lexical initialization stays even when its first value is dead, or when
  // an initial undefined read prevents replacing it with the first assignment.
  if(form==5) {test::Check(plan.omitted[body[0].value]);test::Check(plan.statements[body[count+1].value].has_value());}
  else test::Check(!plan.omitted[body[0].value]);
  if(count<=1) {
    test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work})));
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}
}
int main(int argc,char**) {
  for(unsigned form=0;form<7;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
      const auto work=Check(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);
      previous=work;
      if(count==0)Check(form,count,reverse,false,true);
    }
  }
}
