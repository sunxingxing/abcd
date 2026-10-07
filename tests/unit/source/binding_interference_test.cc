#include "../../../src/source/binding_interference.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/family_print.h"
#include "abcd/source/tree_printer.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok,std::source_location at=std::source_location::current()){
  if(!ok){std::cerr<<"binding interference check "<<at.line()<<'\n';std::abort();}}
enum class Form { kStraight,kObserved,kUpdateThrows,kAfterThrows,kAwait,kYield,kBranch,kLoop,kLocal,kCapture,kShadow,kInitialUndefined,kSharedWrite,kLiveUndefined,kRepeatedUndefined,kUndefinedBeforeRoot };
struct Fixture {
  FunctionTree tree{};
  Fixture(Form form){
    tree.callable=form==Form::kAwait?CallableKind::kAsyncFunction:form==Form::kYield?CallableKind::kGenerator:CallableKind::kFunction;
    tree.strictness=Strictness::kStrict;tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"input",u"report",u"flag",u"current",u"scratch",u"error"};tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<3;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
    for(unsigned i=3;i<5;++i)tree.symbols.push_back({{1},{i},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    Stmt(BlockStmt{{1},0,0});
  }
  ExprId Expr(TreeExpression value){ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;}
  StmtId Stmt(TreeStatement value){StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;}
  ExprId Read(unsigned symbol){return Expr(SymbolExpr{{symbol}});}
  ExprId Store(unsigned symbol,ExprId value){return Expr(AssignmentExpr{SymbolTarget{{symbol}},value});}
  StmtId Write(unsigned symbol,ExprId value){return Stmt(ExpressionStmt{Store(symbol,value)});}
  ExprId Call(std::vector<ExprId> arguments){const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.insert(tree.arguments.end(),arguments.begin(),arguments.end());return Expr(CallExpr{Read(1),{first,static_cast<std::uint32_t>(arguments.size())},CallReference::kValue});}
  StmtId Block(std::vector<StmtId> children,ScopeId parent={1}){
    ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,parent});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  }
  void Finish(std::vector<StmtId> children,bool reverse){
    auto& block=std::get<BlockStmt>(tree.statements[0].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    if(!reverse)return;
    const auto map=[&](StmtId id){return StmtId{static_cast<std::uint32_t>(tree.statements.size()-id.value-1)};};
    for(auto& child:tree.children)child=map(child);
    for(auto& statement:tree.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,IfStmt>){node.consequent=map(node.consequent);if(node.alternate)node.alternate=map(*node.alternate);}
      else if constexpr(std::is_same_v<T,LoopStmt>)node.body=map(node.body);
      else if constexpr(std::is_same_v<T,BreakStmt>||std::is_same_v<T,ContinueStmt>)node.target=map(node.target);
      else if constexpr(std::is_same_v<T,TryStmt>){node.body=map(node.body);if(node.handler)node.handler->body=map(node.handler->body);if(node.finalizer)node.finalizer=map(*node.finalizer);}
    },statement.statement);
    tree.body=map(tree.body);std::reverse(tree.statements.begin(),tree.statements.end());
  }
};
std::uint64_t Run(Form form,unsigned count,bool reverse,bool emit){
  Fixture f(form);auto& tree=f.tree;
  if(form==Form::kLocal||form==Form::kCapture){tree.symbols[3].kind=BindingKind::kLocal;tree.symbols[3].temporary.reset();}
  const bool live_undefined=form==Form::kLiveUndefined||form==Form::kRepeatedUndefined;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}}),f.Write(3,form==Form::kLoop||live_undefined?f.Expr(TreeLiteral{IntegerLiteral{0}}):f.Read(0))};
  std::vector<StmtId> operations;
  const auto empty=roots[1];
  if(live_undefined) {
    roots={roots[0],roots[2],f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})};
    if(form==Form::kRepeatedUndefined)operations.push_back(empty);else roots.push_back(empty);
    operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}));
    operations.push_back(f.Write(3,f.Read(0)));
  } else if(form==Form::kUndefinedBeforeRoot) {
    operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}));
    operations.push_back(roots.back());roots.pop_back();
  }
  if(form==Form::kInitialUndefined){std::swap(roots[1],roots[2]);operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}));}
  operations.push_back(f.Write(4,f.Read(3)));
  for(unsigned i=0;i<count;++i)operations.push_back(f.Block({}));
  const auto update=f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix})});
  if(form==Form::kShadow){
    const auto block=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[block.value].statement).scope;
    tree.symbols.push_back({scope,{3},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    tree.children.push_back(f.Stmt(DeclarationStmt{{5},f.Expr(TreeLiteral{IntegerLiteral{100}})}));tree.children.push_back(update);
    std::get<BlockStmt>(tree.statements[block.value].statement).child_count=2;operations.push_back(block);
  }else operations.push_back(update);
  if(form==Form::kCapture){tree.closure_captures={{3}};operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Expr(FunctionExpr{{1},{0,1}})})}));}
  if(form==Form::kObserved)operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  if(form==Form::kAfterThrows)operations.push_back(f.Stmt(ExpressionStmt{f.Call({})}));
  if(form==Form::kAwait)operations.push_back(f.Stmt(ExpressionStmt{f.Expr(AwaitExpr{f.Call({})})}));
  if(form==Form::kYield)operations.push_back(f.Stmt(ExpressionStmt{f.Expr(YieldExpr{f.Read(4)})}));
  if(form==Form::kBranch)operations.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Stmt(ReturnStmt{f.Read(3)})}),{}}));
  if(form==Form::kSharedWrite)operations.push_back(f.Write(4,f.Call({f.Read(4)})));
  operations.push_back(f.Write(3,f.Read(4)));
  if(form==Form::kLoop){
    const auto loop=f.Stmt(LoopStmt{});
    const auto exit=f.Block({f.Stmt(BreakStmt{loop})});
    operations.insert(operations.begin(),f.Stmt(IfStmt{f.Expr(BinaryExpr{analysis::BinaryOperator::kGreaterEqual,f.Read(3),f.Read(0)}),exit,{}}));
    const auto body=f.Block(operations);std::get<LoopStmt>(tree.statements[loop.value].statement).body=body;
    tree.scopes[std::get<BlockStmt>(tree.statements[exit.value].statement).scope.value].parent=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    roots.push_back(loop);
  } else if(form==Form::kRepeatedUndefined) {
    const auto loop=f.Stmt(LoopStmt{});
    const auto exit=f.Block({f.Stmt(BreakStmt{loop})});
    operations.push_back(f.Stmt(IfStmt{f.Read(2),exit,{}}));
    operations.push_back(f.Write(2,f.Expr(TreeLiteral{BooleanLiteral{true}})));
    const auto body=f.Block(operations);std::get<LoopStmt>(tree.statements[loop.value].statement).body=body;
    const auto scope=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    tree.symbols[4].scope=scope;tree.scopes[std::get<BlockStmt>(tree.statements[exit.value].statement).scope.value].parent=scope;
    roots.push_back(loop);
  } else if(form==Form::kUpdateThrows||form==Form::kAfterThrows||form==Form::kAwait||form==Form::kYield){
    const auto body=f.Block(operations);
    if(form==Form::kYield)roots.push_back(f.Stmt(TryStmt{body,{},f.Block({f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})})}));
    else {
      const auto caught=f.Block({f.Stmt(ReturnStmt{f.Read(3)})});
      const auto scope=std::get<BlockStmt>(tree.statements[caught.value].statement).scope;tree.scopes[scope.value].kind=ScopeKind::kCatch;
      tree.symbols.push_back({scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
      roots.push_back(f.Stmt(TryStmt{body,CatchClause{{5},caught},{}}));
    }
  } else roots.insert(roots.end(),operations.begin(),operations.end());
  roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));
  // Empty blocks are real siblings under the owning protected/loop block.
  for(const auto& statement:tree.statements)if(const auto* parent=std::get_if<BlockStmt>(&statement.statement))
    for(unsigned i=0;i<parent->child_count;++i)if(const auto* child=std::get_if<BlockStmt>(&tree.statements[tree.children[parent->first_child+i].value].statement))tree.scopes[child->scope.value].parent=parent->scope;
  f.Finish(roots,reverse);
  const auto structure=VerifyTreeStructure(tree);
  if(auto* error=std::get_if<TreeStructureError>(&structure))std::cerr<<"structure form "<<unsigned(form)<<" count "<<count<<" error "<<unsigned(*error)<<'\n';
  Check(std::holds_alternative<TreeStructure>(structure));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  auto planned=PlanBindingInitializers(tree,loops,nullish);
  if(auto* error=std::get_if<TreeStructureError>(&planned))std::cerr<<"form "<<unsigned(form)<<" error "<<unsigned(*error)<<'\n';
  Check(std::holds_alternative<BindingInitializers>(planned));const auto& bindings=std::get<BindingInitializers>(planned);
  const bool merged=form==Form::kStraight||form==Form::kUpdateThrows||form==Form::kLoop||form==Form::kSharedWrite||live_undefined;
  if(bool(bindings.storage[4])!=merged)std::cerr<<"form "<<unsigned(form)<<" count "<<count<<" reverse "<<reverse<<" merged "<<bool(bindings.storage[4])<<'\n';
  Check(bool(bindings.storage[4])==merged);
  const auto empty_index=reverse?tree.statements.size()-1-empty.value:empty.value;
  Check(bindings.undefined_stores[empty_index].has_value()==live_undefined);
  if(live_undefined)Check(!bindings.omitted[empty_index]);
  if(live_undefined&&count==0) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;
  if(form==Form::kCapture){
    FunctionTree child{};child.owner={1};child.callable=CallableKind::kFunction;child.strictness=Strictness::kStrict;
    child.names={u"current"};child.parameter_scope={0};child.body_scope={1};child.capture_scope=ScopeId{2};child.body={0};
    child.scopes={{ScopeKind::kParameters,ScopeId{2}},{ScopeKind::kFunctionBody,ScopeId{0}},{ScopeKind::kCaptures,{}}};
    child.symbols={{{2},{0},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kSynthesized,{}}};child.captures={{{0},{tree.owner,{3}}}};
    child.origins={OriginalSite{{0},{0}}};child.expressions={{SymbolExpr{{0}},{0}}};
    child.statements={{BlockStmt{{1},0,1},{0}},{ReturnStmt{ExprId{0}},{0}}};child.children={{1}};
    ConstructedFamily family;family.root={1};family.members={{std::move(child),{},{}},{tree,{},{}}};
    Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family,output)));
  }else Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  Check((output.str().find("scratch")==std::string::npos)==merged);
  if(emit)std::cout<<nlohmann::json{{"form",unsigned(form)},{"count",count},{"reverse",reverse},{"merged",merged},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t Many(unsigned count,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;std::vector<StmtId> declarations,body;
  for(unsigned i=0;i<count;++i){
    unsigned a=3,b=4;
    if(i){a=tree.symbols.size();b=a+1;for(const auto prefix:{"current","scratch"}){
      const NameId name{static_cast<std::uint32_t>(tree.names.size())};const auto text=std::string(prefix)+std::to_string(i);tree.names.emplace_back(text.begin(),text.end());
      tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});}}
    declarations.push_back(f.Stmt(DeclarationStmt{{a},{}}));declarations.push_back(f.Stmt(DeclarationStmt{{b},{}}));
    body.push_back(f.Write(a,f.Read(0)));body.push_back(f.Write(b,f.Read(a)));
    body.push_back(f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{b},UpdateDirection::kIncrement,UpdateResult::kPrefix})}));
    body.push_back(f.Write(a,f.Read(b)));body.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(a)})}));
  }
  declarations.insert(declarations.end(),body.begin(),body.end());declarations.push_back(f.Stmt(ReturnStmt{}));f.Finish(declarations,false);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  unsigned aliases=0;for(const auto alias:bindings.storage)aliases+=alias.has_value();Check(aliases==count);
  Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  Check(output.str().find("scratch")==std::string::npos);
  if(emit)std::cout<<nlohmann::json{{"count",count},{"work",bindings.work},{"source",output.str()}}<<'\n';
  return bindings.work;
}
}
int main(int argc,char** argv){
  if(argc>1&&std::string_view(argv[1])=="--scale"){
    std::uint64_t previous=0;for(unsigned count:{1U,200U,201U,400U,800U}){
      const auto work=Many(count,true);if(count==800)Check(work<previous*3);previous=work;}return 0;
  }
for(unsigned form=0;form<=unsigned(Form::kUndefinedBeforeRoot);++form){std::uint64_t previous=0;for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}){
  const auto work=Run(Form(form),count,reverse,argc>1);if(!reverse){if(count==800)Check(work<previous*3);previous=work;}}}}
