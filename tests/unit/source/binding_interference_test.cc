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
#include <atomic>
#include <functional>
#include <thread>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok,std::source_location at=std::source_location::current()){
  if(!ok){std::cerr<<"binding interference check "<<at.line()<<'\n';std::abort();}}
// Fixture cases own disjoint trees and plans. Two fixed workers bound live
// preparation; serial mode remains available for deterministic comparison.
// Each job keeps its complete geometric sequence and all exact-budget checks.
thread_local std::ostream* fixture_output=&std::cout;
class FixtureBatch {
 public:
  void Add(std::function<void()> job){jobs_.push_back(std::move(job));}
  void Run(bool parallel) {
    if(!parallel){for(const auto& job:jobs_)job();return;}
    std::vector<std::string> results(jobs_.size());std::atomic<std::size_t> next{0};
    const auto run=[&] {
      for(;;) {
        const auto i=next.fetch_add(1,std::memory_order_relaxed);if(i>=jobs_.size())break;
        std::ostringstream output;fixture_output=&output;jobs_[i]();
        results[i]=std::move(output).str();fixture_output=&std::cout;
      }
    };
    std::thread worker(run);run();worker.join();
    for(const auto& output:results)std::cout<<output;
  }
 private:
  std::vector<std::function<void()>> jobs_;
};
enum class Form { kStraight,kObserved,kUpdateThrows,kAfterThrows,kAwait,kYield,kBranch,kLoop,kLocal,kCapture,kShadow,kInitialUndefined,kSharedWrite,kLiveUndefined,kRepeatedUndefined,kUndefinedBeforeRoot,kElidedReference,kRetainedReference,kForwardedUpdate,kForwardedUpdateThrows,kForwardedShadow,kRoundTrip,kForwardedAliasShadow,kNativeInitializer,kNativeProducer };
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
  const bool native=form==Form::kNativeInitializer||form==Form::kNativeProducer;
  std::vector<StmtId> roots;
  if(native)roots={f.Stmt(DeclarationStmt{{3},form==Form::kNativeProducer?f.Call({f.Read(0)}):f.Read(0)}),f.Stmt(DeclarationStmt{{4},{}})};
  else roots={f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}}),f.Write(3,form==Form::kLoop||live_undefined?f.Expr(TreeLiteral{IntegerLiteral{0}}):f.Read(0))};
  std::vector<StmtId> operations;
  const bool forwarded=form==Form::kForwardedUpdate||form==Form::kForwardedUpdateThrows||form==Form::kForwardedShadow||form==Form::kForwardedAliasShadow;
  if(forwarded) {
    tree.names.push_back(u"snapshot");
    tree.symbols.push_back({{1},{6},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    roots.insert(roots.begin()+2,f.Stmt(DeclarationStmt{{5},{}}));
  }
  const auto empty=roots[1];
  if(form==Form::kElidedReference||form==Form::kRetainedReference) {
    const auto write=f.Store(4,f.Expr(TreeLiteral{IntegerLiteral{0}}));
    const auto call=f.Call(form==Form::kRetainedReference?std::vector<ExprId>{f.Read(4)}:std::vector<ExprId>{});
    roots.insert(roots.end()-1,f.Stmt(ExpressionStmt{f.Expr(SequenceExpr{write,call})}));
  }
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
  if(!forwarded)for(unsigned i=0;i<count;++i)operations.push_back(f.Block({}));
  const auto value=form==Form::kRoundTrip||form==Form::kForwardedAliasShadow?f.Read(4):f.Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix});
  const auto update=forwarded?f.Write(5,value):f.Stmt(ExpressionStmt{form==Form::kRoundTrip?f.Call({value}):value});
  if(form==Form::kShadow){
    const auto block=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[block.value].statement).scope;
    tree.symbols.push_back({scope,{3},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    tree.children.push_back(f.Stmt(DeclarationStmt{{5},f.Expr(TreeLiteral{IntegerLiteral{100}})}));tree.children.push_back(update);
    std::get<BlockStmt>(tree.statements[block.value].statement).child_count=2;operations.push_back(block);
  }else operations.push_back(update);
  if(forwarded)for(unsigned i=0;i<count;++i)operations.push_back(f.Block({}));
  if(form==Form::kCapture){tree.closure_captures={{3}};operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Expr(FunctionExpr{{1},{0,1}})})}));}
  if(form==Form::kObserved)operations.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  if(form==Form::kAfterThrows)operations.push_back(f.Stmt(ExpressionStmt{f.Call({})}));
  if(form==Form::kAwait)operations.push_back(f.Stmt(ExpressionStmt{f.Expr(AwaitExpr{f.Call({})})}));
  if(form==Form::kYield)operations.push_back(f.Stmt(ExpressionStmt{f.Expr(YieldExpr{f.Read(4)})}));
  if(form==Form::kBranch)operations.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Stmt(ReturnStmt{f.Read(3)})}),{}}));
  if(form==Form::kSharedWrite)operations.push_back(f.Write(4,f.Call({f.Read(4)})));
  if(form==Form::kForwardedShadow||form==Form::kForwardedAliasShadow) {
    const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
    tree.symbols.push_back({scope,{form==Form::kForwardedShadow?3U:0U},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto branch=f.Block({},scope);
    const auto condition=f.Stmt(IfStmt{f.Read(5),branch,{}});
    const auto declaration=f.Stmt(DeclarationStmt{shadow,f.Expr(TreeLiteral{IntegerLiteral{100}})});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=2;
    tree.children.push_back(condition);tree.children.push_back(declaration);operations.push_back(inner);
    operations.push_back(f.Write(3,f.Read(4)));
  } else operations.push_back(forwarded?f.Block({f.Write(3,f.Read(5))}):f.Write(3,f.Read(4)));
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
  } else if(form==Form::kUpdateThrows||form==Form::kAfterThrows||form==Form::kAwait||form==Form::kYield||form==Form::kForwardedUpdateThrows){
    const auto body=f.Block(operations);
    if(form==Form::kYield)roots.push_back(f.Stmt(TryStmt{body,{},f.Block({f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})})}));
    else {
      const auto caught=f.Block({f.Stmt(ReturnStmt{f.Read(3)})});
      const auto scope=std::get<BlockStmt>(tree.statements[caught.value].statement).scope;tree.scopes[scope.value].kind=ScopeKind::kCatch;
      const SymbolId parameter{static_cast<std::uint32_t>(tree.symbols.size())};
      tree.symbols.push_back({scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
      roots.push_back(f.Stmt(TryStmt{body,CatchClause{parameter,caught},{}}));
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
  // Catch reads retain the input snapshot at their actual occurrences. Once
  // those facts are published, the semantic back-edge can merge the remaining
  // update storage. The independent executor checks throw/reject and identity.
  const bool merged=native||form==Form::kStraight||form==Form::kAfterThrows||form==Form::kAwait||form==Form::kUpdateThrows||form==Form::kLoop||form==Form::kSharedWrite||form==Form::kInitialUndefined||form==Form::kElidedReference||form==Form::kRetainedReference||form==Form::kRoundTrip||form==Form::kUndefinedBeforeRoot||live_undefined||forwarded;
  if(bool(bindings.storage[4])!=merged)std::cerr<<"form "<<unsigned(form)<<" count "<<count<<" reverse "<<reverse<<" merged "<<bool(bindings.storage[4])<<'\n';
  Check(bool(bindings.storage[4])==merged);
  // Inert conditional reads are gone before motion/storage. Their obsolete
  // destinations cannot constrain retained updates or final entry reads.
  if(form==Form::kRoundTrip){Check(bindings.aliases[3]==SymbolId{0});Check(bindings.Storage({4})==SymbolId{0});}
  if(form==Form::kForwardedAliasShadow){Check(bindings.aliases[3]==SymbolId{0});Check(bindings.Storage({4})==SymbolId{0});}
  if(forwarded) {
    const auto at=reverse?tree.statements.size()-1-update.value:update.value;
    const auto occurrence=std::get<ExpressionStmt>(tree.statements[at].statement).expression;
    if(form==Form::kForwardedShadow||form==Form::kForwardedAliasShadow)Check(!bindings.transports[occurrence.value]);
    else Check(bindings.transports[occurrence.value]&&bindings.transports[occurrence.value]->value==value);
  }
  const auto empty_index=reverse?tree.statements.size()-1-empty.value:empty.value;
  // Completed private reads now consume the initial undefined primitive.
  // Fresh demand/initial-value cleanup removes its dead write; storage merging
  // is independently retained. Repeated activation still reads undefined.
  Check(!bindings.undefined_stores[empty_index]);
  if(live_undefined)Check(bindings.omitted[empty_index]);
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
  if(forwarded)Check(output.str().find("snapshot")==std::string::npos);
  if(form==Form::kRoundTrip)Check(output.str().find("current")==std::string::npos);
  if(form==Form::kForwardedAliasShadow)Check(output.str().find("current")==std::string::npos);
  if(emit)*fixture_output<<nlohmann::json{{"form",unsigned(form)},{"count",count},{"reverse",reverse},{"merged",merged},{"source",output.str()}}<<'\n';
  return bindings.work;
}
// Canonical lexical scope must participate in representative selection, even
// when both initializations dominate every use. An unrelated sibling shadow
// makes the narrower spelling unique; it cannot make that slot visible outside.
std::uint64_t CanonicalScope(unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  const auto outer=f.Stmt(DeclarationStmt{{3},{}});
  const auto inner=f.Block({});
  const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  tree.symbols[4].scope=scope;
  std::vector<StmtId> body{f.Stmt(DeclarationStmt{{4},{}}),
      f.Stmt(ExpressionStmt{f.Call({f.Store(4,f.Read(0))})}),
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Write(3,f.Read(4)),
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})};
  for(unsigned i=0;i<count;++i)body.push_back(f.Block({},scope));
  auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);
  block.first_child=tree.children.size();block.child_count=body.size();
  tree.children.insert(tree.children.end(),body.begin(),body.end());
  const auto shadow=f.Block({});
  const auto shadow_scope=std::get<BlockStmt>(tree.statements[shadow.value].statement).scope;
  const SymbolId local{static_cast<std::uint32_t>(tree.symbols.size())};
  tree.symbols.push_back({shadow_scope,{3},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto declaration=f.Stmt(DeclarationStmt{local,{}});
  auto& sibling=std::get<BlockStmt>(tree.statements[shadow.value].statement);
  sibling.first_child=tree.children.size();sibling.child_count=1;tree.children.push_back(declaration);
  f.Finish({outer,inner,shadow,f.Stmt(ReturnStmt{f.Read(3)})},reverse);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  Check(bindings.storage[4]==SymbolId{3});
  if(count==0) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  Check(output.str().find("scratch")==std::string::npos);
  if(emit)*fixture_output<<nlohmann::json{{"form",25},{"count",count},{"reverse",reverse},{"merged",true},{"source",output.str()}}<<'\n';
  return bindings.work;
}
// Both spellings have shadows, but only one shadow contains group references.
// Global uniqueness cannot choose the feasible name; actual lexical observations can.
std::uint64_t NameScope(bool observed,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}}),
      f.Stmt(ExpressionStmt{f.Call({f.Store(3,f.Read(0))})}),
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Stmt(ExpressionStmt{f.Call({f.Store(4,f.Read(3))})})};
  const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
  tree.symbols.push_back({scope,{3},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  std::vector<StmtId> children{f.Stmt(DeclarationStmt{shadow,{}})};
  const std::vector<StmtId> updates{
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Stmt(ExpressionStmt{f.Call({f.Read(4)})})};
  if(observed)children.insert(children.end(),updates.begin(),updates.end());
  else roots.insert(roots.end(),updates.begin(),updates.end());
  for(unsigned i=0;i<count;++i)children.push_back(f.Block({},scope));
  auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);
  block.first_child=tree.children.size();block.child_count=children.size();
  tree.children.insert(tree.children.end(),children.begin(),children.end());roots.push_back(inner);
  roots.push_back(f.Write(3,f.Read(4)));roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));
  const auto sibling=f.Block({});const auto sibling_scope=std::get<BlockStmt>(tree.statements[sibling.value].statement).scope;
  const SymbolId other{static_cast<std::uint32_t>(tree.symbols.size())};
  tree.symbols.push_back({sibling_scope,{4},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto declaration=f.Stmt(DeclarationStmt{other,{}});
  auto& later=std::get<BlockStmt>(tree.statements[sibling.value].statement);
  later.first_child=tree.children.size();later.child_count=1;tree.children.push_back(declaration);roots.push_back(sibling);
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  Check(bindings.Storage({3})==SymbolId{observed?4U:3U});
  Check(bindings.Storage({4})==SymbolId{observed?4U:3U});
  if(!count) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",observed?26:27},{"count",count},{"reverse",reverse},{"merged",true},{"slot",observed?"scratch":"current"},{"source",output.str()}}<<'\n';
  return bindings.work;
}
// The current private shadow has been elided before storage selection. Its
// original spelling must not block the outer slot; retained source locals do.
std::uint64_t RetiredShadow(bool retired,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),
      f.Stmt(ExpressionStmt{f.Call({f.Store(3,f.Read(0))})}),
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Stmt(DeclarationStmt{{4},f.Read(3)})};
  const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
  tree.symbols.push_back({scope,{3},retired?BindingKind::kCompilerTemporary:BindingKind::kLocal,
      DeclarationKind::kLet,NameEvidence::kSynthesized,retired?std::optional{TemporaryReason::kMultipleUses}:std::nullopt});
  const auto declaration=f.Stmt(DeclarationStmt{shadow,f.Expr(TreeLiteral{IntegerLiteral{100}})});
  std::vector<StmtId> children{declaration,f.Stmt(ExpressionStmt{f.Call({f.Read(shadow.value)})}),
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Stmt(ExpressionStmt{f.Call({f.Read(4)})})};
  for(unsigned i=0;i<count;++i)children.push_back(f.Block({},scope));
  auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);
  block.first_child=tree.children.size();block.child_count=children.size();
  tree.children.insert(tree.children.end(),children.begin(),children.end());roots.push_back(inner);
  roots.push_back(f.Write(3,f.Read(4)));roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));
  const auto later=f.Block({});const auto later_scope=std::get<BlockStmt>(tree.statements[later.value].statement).scope;
  const SymbolId other{static_cast<std::uint32_t>(tree.symbols.size())};
  tree.symbols.push_back({later_scope,{4},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto empty=f.Stmt(DeclarationStmt{other,{}});
  auto& sibling=std::get<BlockStmt>(tree.statements[later.value].statement);
  sibling.first_child=tree.children.size();sibling.child_count=1;tree.children.push_back(empty);roots.push_back(later);
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const auto at=reverse?tree.statements.size()-1-declaration.value:declaration.value;
  Check(bool(bindings.omitted[at])==retired);
  Check(bindings.Storage({3})==SymbolId{3});Check(bindings.Storage({4})==SymbolId{retired?3U:4U});
  if(!count) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",retired?28:29},{"count",count},{"reverse",reverse},{"merged",retired},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t NarrowedShadow(bool loop,bool inside,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},f.Read(0)}),
      f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}),
      f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),
      f.Stmt(DeclarationStmt{{4},f.Read(3)})};
  const auto outer=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[outer.value].statement).scope;
  tree.symbols.push_back({scope,{3},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  const auto declaration=f.Stmt(DeclarationStmt{{5},{}});const auto initializer=f.Write(5,f.Call({f.Read(0)}));
  const auto looping=loop?std::optional{f.Stmt(LoopStmt{})}:std::nullopt;
  std::vector<StmtId> inner;
  std::optional<StmtId> exit,flag_exit;
  if(loop){exit=f.Block({f.Stmt(BreakStmt{*looping})},scope);flag_exit=f.Block({f.Stmt(BreakStmt{*looping})},scope);inner.push_back(f.Stmt(IfStmt{f.Expr(BinaryExpr{analysis::BinaryOperator::kGreaterEqual,f.Read(5),f.Expr(TreeLiteral{IntegerLiteral{2}})}),*exit,{}}));}
  else inner.push_back(initializer);
  inner.push_back(f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{5},UpdateDirection::kIncrement,UpdateResult::kPrefix})}));
  inner.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}));
  if(inside)inner.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}));
  for(unsigned i=0;i<count;++i)inner.push_back(f.Block({},scope));
  if(loop){inner.push_back(f.Stmt(IfStmt{f.Read(2),*flag_exit,{}}));inner.push_back(f.Write(2,f.Expr(TreeLiteral{BooleanLiteral{true}})));}
  const auto nested=f.Block(inner,scope);const auto nested_scope=std::get<BlockStmt>(tree.statements[nested.value].statement).scope;
  for(const auto child:inner)if(auto* block=std::get_if<BlockStmt>(&tree.statements[child.value].statement))tree.scopes[block->scope.value].parent=nested_scope;
  if(exit)tree.scopes[std::get<BlockStmt>(tree.statements[exit->value].statement).scope.value].parent=nested_scope;
  if(flag_exit)tree.scopes[std::get<BlockStmt>(tree.statements[flag_exit->value].statement).scope.value].parent=nested_scope;
  if(loop)std::get<LoopStmt>(tree.statements[looping->value].statement).body=nested;
  const auto report=f.Stmt(ExpressionStmt{f.Call({f.Read(4)})});
  std::vector<StmtId> outer_children{declaration};if(loop)outer_children.push_back(initializer);
  outer_children.push_back(loop?*looping:nested);outer_children.push_back(report);
  auto& block=std::get<BlockStmt>(tree.statements[outer.value].statement);
  block.first_child=tree.children.size();block.child_count=outer_children.size();tree.children.insert(tree.children.end(),outer_children.begin(),outer_children.end());roots.push_back(outer);
  roots.push_back(f.Stmt(ReturnStmt{f.Read(4)}));
  const auto sibling=f.Block({});const auto sibling_scope=std::get<BlockStmt>(tree.statements[sibling.value].statement).scope;
  tree.symbols.push_back({sibling_scope,{4},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto empty=f.Stmt(DeclarationStmt{{6},{}});auto& later=std::get<BlockStmt>(tree.statements[sibling.value].statement);later.first_child=tree.children.size();later.child_count=1;tree.children.push_back(empty);roots.push_back(sibling);
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  Check(bindings.Storage({3})==SymbolId{3});Check(bindings.Storage({4})==SymbolId{inside?4U:3U});
  if(loop){const auto at=reverse?tree.statements.size()-1-looping->value:looping->value;Check(bindings.loop_declarations[at].has_value());}
  if(!count) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",30+(loop?1:0)+(inside?2:0)},{"count",count},{"reverse",reverse},{"merged",!inside},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t RetiringCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;tree.symbols[3].kind=BindingKind::kLocal;tree.symbols[3].temporary.reset();
  const bool retained=form==35,activated=form>=36,conditional=form==37;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},f.Call({f.Read(0)})}),f.Stmt(DeclarationStmt{{4},f.Read(3)})};
  const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  tree.symbols.push_back({scope,{3},retained?BindingKind::kLocal:BindingKind::kCompilerTemporary,DeclarationKind::kLet,
      NameEvidence::kSynthesized,retained?std::nullopt:std::optional{TemporaryReason::kMultipleUses}});
  std::vector<StmtId> children{f.Stmt(DeclarationStmt{{5},f.Read(activated?0:4)})};
  if(conditional){tree.names.push_back(u"snapshot");tree.symbols.push_back({scope,{6},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});}
  if(activated) {
    const auto nested=f.Block({},scope);const auto nested_scope=std::get<BlockStmt>(tree.statements[nested.value].statement).scope;
    const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
    tree.symbols.push_back({nested_scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto declaration=f.Stmt(DeclarationStmt{shadow,f.Expr(TreeLiteral{IntegerLiteral{100}})});
    const auto report=f.Stmt(ExpressionStmt{f.Call({f.Read(5)})});
    auto& block=std::get<BlockStmt>(tree.statements[nested.value].statement);block.first_child=tree.children.size();block.child_count=2;
    tree.children.insert(tree.children.end(),{declaration,report});children.push_back(nested);
    if(conditional)children.push_back(f.Stmt(DeclarationStmt{{6},f.Read(4)}));
    children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(conditional?6:4)})}));
  }else children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}));
  for(unsigned i=0;i<count;++i)children.push_back(f.Block({},scope));
  auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());roots.push_back(inner);
  roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));f.Finish(roots,reverse);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  Check(bindings.Storage({4})==SymbolId{form==34?3U:4U});Check(bindings.Storage({5})==SymbolId{form==34?3U:5U});
  // Earlier storage recovery can share the immutable snapshot with scratch.
  // Its spelling remains visible even when the outer current is shadowed.
  if(conditional)Check(bindings.Storage({6})==SymbolId{4});
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"merged",form==34},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t StorageProposal(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;const bool local=form==39,conflict=form==40;
  const auto update=[&](unsigned binding){return f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{binding},UpdateDirection::kIncrement,UpdateResult::kPrefix})});};
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(ExpressionStmt{f.Call({f.Store(3,f.Read(0))})}),update(3),
      f.Stmt(DeclarationStmt{{4},f.Read(3)}),update(4)};
  const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  tree.symbols.push_back({scope,{3},local?BindingKind::kLocal:BindingKind::kCompilerTemporary,DeclarationKind::kLet,
      NameEvidence::kSynthesized,local?std::nullopt:std::optional{TemporaryReason::kMultipleUses}});
  tree.names.push_back(u"bridge");tree.symbols.push_back({{1},{6},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  roots.push_back(f.Stmt(DeclarationStmt{{6},f.Read(2)}));roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(6)})}));
  std::vector<StmtId> children{f.Stmt(DeclarationStmt{{5},f.Read(2)}),update(5),f.Write(6,f.Read(5)),update(6)};
  if(conflict)children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}));
  children.push_back(f.Write(5,f.Read(6)));children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}));children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}));
  for(unsigned i=0;i<count;++i)children.push_back(f.Block({},scope));
  auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());roots.push_back(inner);
  roots.push_back(f.Write(3,f.Read(4)));roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));f.Finish(roots,reverse);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  Check(bindings.Storage({3})==SymbolId{3});Check(bindings.Storage({4})==SymbolId{form==38?3U:4U});
  Check(bindings.Storage({5})==SymbolId{form==38?6U:5U});Check(bindings.Storage({6})==SymbolId{6});
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"merged",form==38},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t StorageSnapshot(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;const bool local=form==42,conflict=form==43;
  const auto update=[&](unsigned binding){return f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{binding},UpdateDirection::kIncrement,UpdateResult::kPrefix})});};
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(ExpressionStmt{f.Call({f.Store(3,f.Read(0))})}),update(3)};
  const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  tree.symbols.push_back({scope,{3},local?BindingKind::kLocal:BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,local?std::nullopt:std::optional{TemporaryReason::kMultipleUses}});
  tree.names.push_back(u"bridge");tree.symbols.push_back({{1},{6},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  roots.push_back(f.Stmt(DeclarationStmt{{6},f.Read(2)}));roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(6)})}));
  roots.push_back(f.Stmt(DeclarationStmt{{4},f.Expr(UnaryExpr{TreeUnary::kNegate,f.Read(3)})}));
  std::vector<StmtId> children;for(unsigned i=0;i<count;++i)children.push_back(f.Block({},scope));
  const auto read=f.Read(4);children.push_back(f.Stmt(IfStmt{read,f.Block({f.Stmt(ExpressionStmt{f.Call({f.Read(2)})})},scope),{}}));
  children.push_back(f.Stmt(DeclarationStmt{{5},f.Read(2)}));children.push_back(update(5));children.push_back(f.Write(6,f.Read(5)));children.push_back(update(6));
  if(conflict)children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}));
  children.push_back(f.Write(5,f.Read(6)));children.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}));
  auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());roots.push_back(inner);
  roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  if(bindings.Storage({5})!=SymbolId{form==41?6U:5U}||bindings.replacements[read.value].has_value()!=(form==41)) {
    std::cerr<<"snapshot form "<<form<<" root "<<bindings.Storage({5}).value<<" replacement "<<bindings.replacements[read.value].has_value()<<'\n';
    std::ostringstream probe;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,probe)));std::cerr<<probe.str();
  }
  Check(bindings.Storage({5})==SymbolId{form==41?6U:5U});Check(bindings.replacements[read.value].has_value()==(form==41));
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"merged",form==41},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t JoinedCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;const bool invariant=form==44||form==46||form==48||form==49;
  const bool known=invariant||form==52;
  if(form==52)tree.strictness=Strictness::kSloppy;
  if(form==50){tree.symbols[4].kind=BindingKind::kLocal;tree.symbols[4].temporary.reset();}
  const auto declaration=f.Stmt(DeclarationStmt{{4},form==48?std::optional{f.Read(0)}:std::nullopt});
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),declaration};for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  if(form==46)roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}));
  const auto value=[&](unsigned source){const auto read=f.Read(source);return form==49?f.Expr(SequenceExpr{f.Call({f.Read(2)}),read}):read;};
  if(form==48){const auto loop=f.Stmt(LoopStmt{});const auto exit=f.Block({f.Stmt(BreakStmt{loop})});
    const auto body=f.Block({f.Stmt(IfStmt{f.Expr(UnaryExpr{TreeUnary::kNot,f.Read(2)}),exit,{}}),f.Write(4,f.Read(0)),f.Write(2,f.Expr(TreeLiteral{BooleanLiteral{false}}))});
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=body;
    tree.scopes[std::get<BlockStmt>(tree.statements[exit.value].statement).scope.value].parent=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    roots.push_back(loop);}
  else{const auto first=f.Block({f.Write(4,value(0))});const auto other=f.Block({f.Write(4,value(form==45?2:0))});roots.push_back(f.Stmt(IfStmt{f.Read(2),first,other}));}
  if(form==47)roots.push_back(f.Write(0,f.Read(2)));
  const auto final_read=f.Read(4);
  if(form==51){const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto init=f.Stmt(DeclarationStmt{{5},f.Read(2)}),report=f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}),result=f.Stmt(ReturnStmt{final_read});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=3;tree.children.insert(tree.children.end(),{init,report,result});roots.push_back(inner);}
  else roots.push_back(f.Stmt(ReturnStmt{final_read}));
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const auto at=reverse?tree.statements.size()-1-declaration.value:declaration.value;
  // Current-unit reads can retire a carrier without an all-definition
  // invariant. Mapped entry cells must never borrow the latter certificate.
  Check(bool(bindings.omitted[at])==known);
  Check(bindings.ReadBinding(final_read,{4})==(known?SymbolId{0}:bindings.Storage({4})));
  if(!count){Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);}
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"merged",known},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t SharedCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  const auto first=f.Stmt(DeclarationStmt{{3},{}}),second=f.Stmt(DeclarationStmt{{4},{}});
  std::vector<StmtId> roots{first,second};for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  const auto branch=[&](unsigned target,unsigned source,unsigned other){const auto value=[&](unsigned binding){const auto read=f.Read(binding);return form==56&&target==3?f.Expr(SequenceExpr{f.Call({f.Read(2)}),read}):read;};
    roots.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Write(target,value(source))}),f.Block({f.Write(target,value(other))})}));};
  branch(3,0,0);roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));branch(4,3,3);
  if(form==54)roots.push_back(f.Write(0,f.Read(2)));
  const auto final_read=f.Read(4);
  if(form==55){const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto init=f.Stmt(DeclarationStmt{{5},f.Read(2)}),report=f.Stmt(ExpressionStmt{f.Call({f.Read(5)})}),result=f.Stmt(ReturnStmt{final_read});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=3;tree.children.insert(tree.children.end(),{init,report,result});roots.push_back(inner);}
  else roots.push_back(f.Stmt(ReturnStmt{final_read}));
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool known=form==53||form==56;
  const auto at=[&](StmtId id){return reverse?tree.statements.size()-1-id.value:id.value;};
  if(!known)Check(bindings.Storage({3})==bindings.Storage({4}));
  Check(unsigned(bool(bindings.omitted[at(first)]))+unsigned(bool(bindings.omitted[at(second)]))==(known?2U:1U));
  Check(bindings.ReadBinding(final_read,{4})==(known?SymbolId{0}:bindings.Storage({4})));
  if(!count){Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);}
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"merged",known},{"slot",bindings.Storage({3}).value},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t ComposedCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}})};
  if(form!=68)roots.push_back(f.Stmt(DeclarationStmt{{4},{}}));
  roots.push_back(f.Write(3,f.Read(0)));
  roots.push_back(f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPrefix})}));
  for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  const auto sequence=[&](){return f.Expr(SequenceExpr{f.Call({f.Read(2)}),f.Read(3)});};
  ExprId value{};
  if(form==58||form==65)value=f.Expr(ConditionalExpr{f.Read(2),sequence(),form==65?f.Read(0):sequence()});
  else if(form>=59&&form<=61)value=f.Expr(LogicalExpr{form==59?TreeLogical::kOr:form==60?TreeLogical::kAnd:TreeLogical::kNullish,f.Read(3),sequence()});
  else if(form==62||form==66)value=f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,form==62?UpdateResult::kPrefix:UpdateResult::kPostfix});
  else if(form==63)value=f.Store(3,f.Read(0));
  else value=sequence();
  std::vector<StmtId> work{form==68?f.Stmt(DeclarationStmt{{4},value}):f.Write(4,value),f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{4},UpdateDirection::kIncrement,UpdateResult::kPrefix})})};
  if(form==63||form==66)work.insert(work.begin()+1,f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  if(form==64||form==65||form==66)work.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  if(form==67){const auto body=f.Block(work),handler=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[handler.value].statement).scope;
    tree.scopes[scope.value].kind=ScopeKind::kCatch;tree.symbols.push_back({scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto report=f.Stmt(ExpressionStmt{f.Call({f.Read(3),f.Read(4)})}),result=f.Stmt(ReturnStmt{f.Read(3)});
    auto& block=std::get<BlockStmt>(tree.statements[handler.value].statement);block.first_child=tree.children.size();block.child_count=2;tree.children.insert(tree.children.end(),{report,result});
    roots.push_back(f.Stmt(TryStmt{body,CatchClause{{5},handler},{}}));}
  else roots.insert(roots.end(),work.begin(),work.end());
  roots.push_back(f.Stmt(ReturnStmt{f.Read(4)}));f.Finish(roots,reverse);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool shared=form<=63||form==68;
  if((bindings.Storage({3})==bindings.Storage({4}))!=shared) {
    std::cerr<<"composed form "<<form<<" count "<<count<<" reverse "<<reverse<<" roots "<<bindings.Storage({3}).value<<' '<<bindings.Storage({4}).value<<'\n';
    std::ostringstream probe;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,probe)));std::cerr<<probe.str();
  }
  Check((bindings.Storage({3})==bindings.Storage({4}))==shared);
  if(!count){Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);}
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"merged",shared},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t ReadCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  if(form==74){tree.symbols[3].kind=BindingKind::kLocal;tree.symbols[3].temporary.reset();}
  if(form==75)tree.strictness=Strictness::kSloppy;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}})};
  if(form==70)roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  roots.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Write(3,f.Read(0))}),f.Block({f.Write(3,f.Read(2))})}));
  roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  if(form==71||form==76){
    const auto handler=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[handler.value].statement).scope;
    tree.scopes[scope.value].kind=ScopeKind::kCatch;
    tree.symbols.push_back({scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto report=f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}),result=f.Stmt(ReturnStmt{f.Read(3)});
    auto& block=std::get<BlockStmt>(tree.statements[handler.value].statement);block.first_child=tree.children.size();block.child_count=2;tree.children.insert(tree.children.end(),{report,result});
    std::vector<StmtId> work;
    if(form==71)work={f.Write(3,f.Read(0)),f.Stmt(ExpressionStmt{f.Call({f.Read(2)})}),f.Write(3,f.Read(2)),f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})};
    else work={f.Write(3,f.Expr(SequenceExpr{f.Call({f.Read(2)}),f.Read(0)}))};
    roots.push_back(f.Stmt(TryStmt{f.Block(work),CatchClause{{5},handler},{}}));
  }
  if(form!=76)roots.push_back(f.Write(3,f.Read(0)));
  if(form==72)roots.push_back(f.Write(0,f.Read(2)));
  const auto read=f.Read(3);
  if(form==73){const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto init=f.Stmt(DeclarationStmt{{5},f.Read(2)}),result=f.Stmt(ReturnStmt{read});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=2;tree.children.insert(tree.children.end(),{init,result});roots.push_back(inner);
  }else roots.push_back(f.Stmt(ReturnStmt{read}));
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool known=form<=71||form==75||form==76;
  const auto actual_read=bindings.ReadBinding(read,SymbolId{3});
  if(actual_read!=(known?SymbolId{0}:bindings.Storage(SymbolId{3})))
    std::cerr<<"read copy form="<<form<<" count="<<count<<" actual="<<actual_read.value<<" storage="<<bindings.Storage({3}).value<<'\n';
  Check(actual_read==(known?SymbolId{0}:bindings.Storage(SymbolId{3})));
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);
  Check(std::holds_alternative<PrintedFunction>(printed));
  Check((output.str().rfind("return input;")!=std::string::npos)==known);
  if(!count){Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);}
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"partial",true},{"merged",known},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t AvailableCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  if(form==83)tree.strictness=Strictness::kSloppy;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}})};
  for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  const auto value=[&]{return form==86||form==87?f.Expr(UpdateExpr{{0},UpdateDirection::kIncrement,form==86?UpdateResult::kPrefix:UpdateResult::kPostfix}):f.Read(0);};
  const auto copy=[&]{roots.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Write(3,value())}),f.Block({f.Write(3,value())})}));};
  if(form==78)roots.push_back(f.Write(0,f.Read(2)));
  copy();
  if(form==79||form==82)roots.push_back(f.Write(0,f.Read(2)));
  if(form==80)roots.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Write(0,f.Read(2))}),{}}));
  if(form==82)copy();
  const auto first=f.Read(3);
  if(form==85){
    const auto loop=f.Stmt(LoopStmt{}),body=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    const auto exit=f.Block({f.Stmt(BreakStmt{loop})},scope);
    const auto guard=f.Stmt(IfStmt{f.Expr(UnaryExpr{TreeUnary::kNot,f.Read(2)}),exit,{}});
    const auto copies=f.Stmt(IfStmt{f.Read(2),f.Block({f.Write(3,f.Read(0))},scope),f.Block({f.Write(3,f.Read(0))},scope)});
    const std::vector<StmtId> children{guard,copies,f.Stmt(ExpressionStmt{f.Call({first})}),f.Write(0,f.Read(2)),f.Write(2,f.Expr(TreeLiteral{BooleanLiteral{false}}))};
    auto& block=std::get<BlockStmt>(tree.statements[body.value].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=body;roots.push_back(loop);
  }else roots.push_back(f.Stmt(ExpressionStmt{f.Call({first})}));
  if(form==77||form==83||form==86||form==87)roots.push_back(f.Write(0,f.Read(2)));
  if(form==81){
    const auto handler=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[handler.value].statement).scope;
    tree.scopes[scope.value].kind=ScopeKind::kCatch;
    tree.symbols.push_back({scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto caught=f.Stmt(ExpressionStmt{f.Call({f.Read(3)})});
    auto& block=std::get<BlockStmt>(tree.statements[handler.value].statement);block.first_child=tree.children.size();block.child_count=1;tree.children.push_back(caught);
    roots.push_back(f.Stmt(TryStmt{f.Block({f.Write(0,f.Expr(SequenceExpr{f.Call({f.Read(2)}),f.Read(2)}))}),CatchClause{{5},handler},{}}));
  }
  roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}));
  const auto last=f.Read(3);
  if(form==84){
    const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto init=f.Stmt(DeclarationStmt{{5},f.Read(2)}),returned=f.Stmt(ReturnStmt{last});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=2;tree.children.insert(tree.children.end(),{init,returned});
    roots.push_back(inner);roots.push_back(f.Write(0,f.Read(2)));
  }else roots.push_back(f.Stmt(ReturnStmt{last}));
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool first_known=form==77||form==78||form==81||form==82||form==83||form==84||form==85||form==86,last_known=form==78||form==82;
  Check(bindings.ReadBinding(first,{3})==(first_known?SymbolId{0}:bindings.Storage({3})));
  Check(bindings.ReadBinding(last,{3})==(last_known?SymbolId{0}:bindings.Storage({3})));
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(!count){Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);}
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"available",true},{"first",first_known},{"last",last_known},{"source",output.str()}}<<'\n';
  return bindings.work;
}
std::uint64_t TransitiveCopies(unsigned form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  if(form==94)tree.strictness=Strictness::kSloppy;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}})};
  for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  const auto copies=[&](std::vector<StmtId>& nodes,unsigned target,unsigned source,ScopeId scope=ScopeId{1}){
    const auto value=[&]{
      if(target==4&&form==95)return f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPostfix});
      if(target==4&&form==91)return f.Expr(SequenceExpr{f.Call({f.Read(2)}),f.Read(source)});
      return f.Read(source);
    };
    nodes.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Write(target,value())},scope),
        f.Block({f.Write(target,target==4&&form==92?f.Read(2):value())},scope)}));
  };
  copies(roots,3,0);
  if(form==89)roots.push_back(f.Write(0,f.Read(2)));
  const auto first=f.Read(4),second=f.Read(4);
  if(form==93){
    const auto loop=f.Stmt(LoopStmt{}),body=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    const auto exit=f.Block({f.Stmt(BreakStmt{loop})},scope);
    std::vector<StmtId> children{f.Stmt(IfStmt{f.Expr(UnaryExpr{TreeUnary::kNot,f.Read(2)}),exit,{}})};
    copies(children,3,0,scope);copies(children,4,3,scope);
    children.insert(children.end(),{f.Stmt(ExpressionStmt{f.Call({first})}),f.Write(3,f.Read(2)),f.Stmt(ExpressionStmt{f.Call({f.Read(3)})}),f.Write(2,f.Expr(TreeLiteral{BooleanLiteral{false}}))});
    auto& block=std::get<BlockStmt>(tree.statements[body.value].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=body;roots.push_back(loop);
  }else{
    if(form==91){
      const auto tried=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[tried.value].statement).scope;
      std::vector<StmtId> children;copies(children,4,3,scope);
      auto& block=std::get<BlockStmt>(tree.statements[tried.value].statement);block.first_child=tree.children.size();block.child_count=children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
      const auto handler=f.Block({});const auto caught_scope=std::get<BlockStmt>(tree.statements[handler.value].statement).scope;tree.scopes[caught_scope.value].kind=ScopeKind::kCatch;
      tree.symbols.push_back({caught_scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
      const std::vector<StmtId> caught{f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}),f.Stmt(ReturnStmt{f.Read(4)})};
      auto& catcher=std::get<BlockStmt>(tree.statements[handler.value].statement);catcher.first_child=tree.children.size();catcher.child_count=caught.size();tree.children.insert(tree.children.end(),caught.begin(),caught.end());
      roots.push_back(f.Stmt(TryStmt{tried,CatchClause{{5},handler},{}}));
    }else copies(roots,4,3);
    if(form==90)roots.insert(roots.end(),{f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{3},UpdateDirection::kIncrement,UpdateResult::kPrefix})}),f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})});
    roots.push_back(f.Stmt(ExpressionStmt{f.Call({first})}));
    if(form!=90)roots.insert(roots.end(),{f.Write(3,f.Read(2)),f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})});
  }
  roots.push_back(f.Stmt(ExpressionStmt{f.Call({second})}));roots.push_back(f.Write(0,f.Read(2)));
  const auto last=f.Read(4);roots.insert(roots.end(),{f.Stmt(ExpressionStmt{f.Call({f.Read(4)})}),f.Stmt(ReturnStmt{last})});
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto bindings=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool known=form==88||form==90||form==91||form==93;
  // Mapped parameters are current units before a callback, even though
  // the callback invalidates the later parameter read. Entry-domain proof
  // remains independent of this occurrence copy.
  const bool first_known=known||form==94;
  Check(bindings.ReadBinding(first,{4})==(first_known?SymbolId{0}:bindings.Storage({4})));
  if(form!=93)Check(bindings.ReadBinding(second,{4})==(known?SymbolId{0}:bindings.Storage({4})));
  Check(bindings.ReadBinding(last,{4})==bindings.Storage({4}));
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(!count){Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);}
  if(emit)*fixture_output<<nlohmann::json{{"form",form},{"count",count},{"reverse",reverse},{"transitive",true},{"first",first_known},{"source",output.str()}}<<'\n';
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
  const auto planned=PlanBindingInitializers(tree,loops,nullish);
  if(const auto* error=std::get_if<TreeStructureError>(&planned))std::cerr<<"scale count="<<count<<" error="<<static_cast<unsigned>(*error)<<'\n';
  Check(std::holds_alternative<BindingInitializers>(planned));
  const auto& bindings=std::get<BindingInitializers>(planned);
  unsigned aliases=0;for(const auto alias:bindings.storage)aliases+=alias.has_value();Check(aliases==count);
  const auto bytes_short=PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work});
  if(std::holds_alternative<BindingInitializers>(bytes_short))std::cerr<<"scale count="<<count<<" bytes-short accepted reported="<<bindings.allocated_bytes<<'\n';
  Check(std::holds_alternative<TreeStructureError>(bytes_short));
  Check(std::get<TreeStructureError>(bytes_short)==TreeStructureError::kResourceLimit);
  const auto work_short=PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1});
  if(std::holds_alternative<BindingInitializers>(work_short))std::cerr<<"scale count="<<count<<" work-short accepted reported="<<bindings.work<<'\n';
  Check(std::holds_alternative<TreeStructureError>(work_short));
  Check(std::get<TreeStructureError>(work_short)==TreeStructureError::kWorkLimit);
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);
  if(const auto* error=std::get_if<TreePrintError>(&printed))
    std::cerr<<"scale count="<<count<<" print error="<<static_cast<unsigned>(*error)<<'\n';
  Check(std::holds_alternative<PrintedFunction>(printed));
  Check(output.str().find("scratch")==std::string::npos);
  if(emit)*fixture_output<<nlohmann::json{{"count",count},{"work",bindings.work},{"bytes",bindings.allocated_bytes},{"attempts",bindings.cost.attempts},{"commits",bindings.cost.commits},{"projections",bindings.cost.projections},{"nodes",bindings.cost.nodes},{"edges",bindings.cost.edges},{"live_facts",bindings.cost.live_facts},{"witnesses",bindings.cost.witnesses},{"source",output.str()}}<<'\n';
  Check(bindings.cost.commits<=bindings.cost.witnesses+2);
  return bindings.work;
}

std::uint64_t Chain(unsigned count,bool conflict,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  std::vector<unsigned> slots{4};
  for(unsigned i=1;i<count;++i){
    const NameId name{static_cast<std::uint32_t>(tree.names.size())};const auto label="copy"+std::to_string(i);
    tree.names.emplace_back(label.begin(),label.end());slots.push_back(tree.symbols.size());
    tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  }
  std::vector<StmtId> body{f.Stmt(DeclarationStmt{{3},{}})};
  for(auto slot:slots)body.push_back(f.Stmt(DeclarationStmt{{slot},{}}));
  body.push_back(f.Write(3,f.Read(0)));unsigned previous=3;
  for(auto slot:slots){body.push_back(f.Write(slot,f.Read(previous)));body.push_back(f.Stmt(ExpressionStmt{f.Call({f.Read(slot)})}));previous=slot;}
  body.push_back(f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{previous},UpdateDirection::kIncrement,UpdateResult::kPrefix})}));
  std::optional<ExprId> snapshot;
  if(conflict){snapshot=f.Read(3);body.push_back(f.Stmt(ExpressionStmt{f.Call({*snapshot})}));}
  body.push_back(f.Write(3,f.Read(previous)));body.push_back(f.Stmt(ReturnStmt{f.Read(3)}));
  if(reverse){
    const auto map=[&](SymbolId id){return SymbolId{static_cast<std::uint32_t>(tree.symbols.size()-1-id.value)};};
    for(auto& parameter:tree.parameters)parameter.symbol=map(parameter.symbol);
    for(auto& expression:tree.expressions)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,SymbolExpr>)value.symbol=map(value.symbol);
      else if constexpr(std::is_same_v<T,AssignmentExpr>)std::get<SymbolTarget>(value.target).symbol=map(std::get<SymbolTarget>(value.target).symbol);
      else if constexpr(std::is_same_v<T,UpdateExpr>)value.symbol=map(value.symbol);
    },expression.expression);
    for(auto& statement:tree.statements)if(auto* declaration=std::get_if<DeclarationStmt>(&statement.statement))declaration->symbol=map(declaration->symbol);
    std::reverse(tree.symbols.begin(),tree.symbols.end());
  }
  f.Finish(body,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);Check(std::holds_alternative<BindingInitializers>(planned));
  const auto& bindings=std::get<BindingInitializers>(planned);
  // Storage sharing and dead-binding deletion are separate responsibilities:
  // retired carriers need no storage map. The independent JS check requires
  // exactly one final declarator for every layout/size, and detects wrong
  // old-value reuse after increment. Keep its occurrence identity here too.
  if(snapshot) {
    const auto mapped=[&](unsigned id){return SymbolId{static_cast<std::uint32_t>(reverse?tree.symbols.size()-1-id:id)};};
    Check(bindings.ReadBinding(*snapshot,mapped(3))==mapped(0));
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)*fixture_output<<nlohmann::json{{"count",count},{"conflict",conflict},{"reverse",reverse},{"work",bindings.work},{"bytes",bindings.allocated_bytes},{"attempts",bindings.cost.attempts},{"commits",bindings.cost.commits},{"projections",bindings.cost.projections},{"nodes",bindings.cost.nodes},{"edges",bindings.cost.edges},{"live_facts",bindings.cost.live_facts},{"witnesses",bindings.cost.witnesses},{"source",output.str()}}<<'\n';
  Check(bindings.cost.commits<=bindings.cost.witnesses+2);
  return bindings.work;
}
}
// Observable roots are current lexical units, never immutable snapshots of a
// scope site. Exercise ordinary/catch allocation and repeated entry through the
// same source adapter consumed by printing; references stay outside the engine.
enum class ScopedForm {kLocal,kCallback,kWrite,kCatch,kCatchExit,kLocalReentry,kCatchReentry,kThrowingUpdate,kCapturedCallback};
std::uint64_t ScopedCopies(ScopedForm form,unsigned count,bool reverse,bool emit){
  Fixture f(Form::kStraight);auto& tree=f.tree;
  const bool caught=form==ScopedForm::kCatch||form==ScopedForm::kCatchExit||form==ScopedForm::kCatchReentry;
  const bool repeated=form==ScopedForm::kLocalReentry||form==ScopedForm::kCatchReentry;
  tree.symbols[3].kind=BindingKind::kLocal;tree.symbols[3].temporary.reset();
  const auto update=[&](unsigned symbol){return f.Expr(UpdateExpr{{symbol},UpdateDirection::kIncrement,UpdateResult::kPrefix});};
  const auto boolean=[&](bool value){return f.Expr(TreeLiteral{BooleanLiteral{value}});};
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{4},{}})},operations;
  if(repeated)roots.push_back(f.Write(2,boolean(false)));
  if(caught){
    const auto handler=f.Block({});
    const auto scope=std::get<BlockStmt>(tree.statements[handler.value].statement).scope;
    tree.scopes[scope.value].kind=ScopeKind::kCatch;
    const SymbolId parameter{3};
    tree.symbols[3]={scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}};
    if(repeated)operations.push_back(f.Stmt(IfStmt{f.Read(2),f.Block({f.Stmt(ReturnStmt{f.Read(4)})},scope),{}}));
    operations.push_back(f.Write(4,f.Read(parameter.value)));
    for(unsigned i=0;i<count;++i)operations.push_back(f.Block({},scope));
    if(repeated)operations.push_back(f.Write(2,boolean(true)));
    else if(form==ScopedForm::kCatch)operations.push_back(f.Stmt(ReturnStmt{f.Read(4)}));
    auto& block=std::get<BlockStmt>(tree.statements[handler.value].statement);
    block.first_child=tree.children.size();block.child_count=operations.size();
    tree.children.insert(tree.children.end(),operations.begin(),operations.end());
    const auto body=f.Block({f.Stmt(ThrowStmt{repeated?update(0):f.Read(0)})});
    const auto statement=f.Stmt(TryStmt{body,CatchClause{parameter,handler},{}});
    if(repeated){const auto loop_body=f.Block({statement});
      const auto loop_scope=std::get<BlockStmt>(tree.statements[loop_body.value].statement).scope;
      tree.scopes[scope.value].parent=loop_scope;
      tree.scopes[std::get<BlockStmt>(tree.statements[body.value].statement).scope.value].parent=loop_scope;
      LoopStmt loop{};loop.body=loop_body;roots.push_back(f.Stmt(loop));
    }else {roots.push_back(statement);if(form==ScopedForm::kCatchExit)roots.push_back(f.Stmt(ReturnStmt{f.Read(4)}));}
  }else if(repeated){
    const auto body=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[body.value].statement).scope;
    tree.symbols[3].scope=scope;
    operations={f.Stmt(DeclarationStmt{{3},update(0)}),
      f.Stmt(IfStmt{f.Read(2),f.Block({f.Stmt(ReturnStmt{f.Read(4)})},scope),{}}),f.Write(4,f.Read(3))};
    for(unsigned i=0;i<count;++i)operations.push_back(f.Block({},scope));
    operations.push_back(f.Write(2,boolean(true)));
    auto& block=std::get<BlockStmt>(tree.statements[body.value].statement);
    block.first_child=tree.children.size();block.child_count=operations.size();
    tree.children.insert(tree.children.end(),operations.begin(),operations.end());
    LoopStmt loop{};loop.body=body;roots.push_back(f.Stmt(loop));
  }else{
    roots.push_back(f.Stmt(DeclarationStmt{{3},f.Read(0)}));
    if(form!=ScopedForm::kThrowingUpdate)roots.push_back(f.Stmt(ExpressionStmt{update(3)}));
    roots.push_back(f.Write(4,f.Read(3)));
    for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
    if(form==ScopedForm::kCallback)roots.push_back(f.Stmt(ExpressionStmt{f.Call({})}));
    if(form==ScopedForm::kCapturedCallback){tree.closure_captures={{3}};
      roots.push_back(f.Stmt(ExpressionStmt{f.Call({f.Expr(FunctionExpr{{1},{0,1}})})}));}
    if(form==ScopedForm::kWrite)roots.push_back(f.Write(3,f.Expr(TreeLiteral{IntegerLiteral{0}})));
    if(form==ScopedForm::kThrowingUpdate){
      const auto body=f.Block({f.Stmt(ExpressionStmt{update(3)})});
      const auto handler=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[handler.value].statement).scope;
      tree.scopes[scope.value].kind=ScopeKind::kCatch;
      const SymbolId parameter{static_cast<std::uint32_t>(tree.symbols.size())};
      tree.symbols.push_back({scope,{5},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
      roots.push_back(f.Stmt(TryStmt{body,CatchClause{parameter,handler},{}}));
    }
    roots.push_back(f.Stmt(ReturnStmt{f.Read(4)}));
  }
  f.Finish(roots,reverse);const auto structure=VerifyTreeStructure(tree);
  if(const auto* error=std::get_if<TreeStructureError>(&structure))std::cerr<<"scoped structure "<<unsigned(form)<<" count "<<count<<" error "<<unsigned(*error)<<'\n';
  Check(std::holds_alternative<TreeStructure>(structure));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);
  if(const auto* error=std::get_if<TreeStructureError>(&planned))std::cerr<<"scoped form "<<unsigned(form)<<" count "<<count<<" error "<<unsigned(*error)<<'\n';
  Check(std::holds_alternative<BindingInitializers>(planned));const auto& bindings=std::get<BindingInitializers>(planned);
  if(count==0){
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes-1,bindings.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{bindings.allocated_bytes,bindings.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;
  if(form==ScopedForm::kCapturedCallback){
    FunctionTree child{};child.owner={1};child.callable=CallableKind::kFunction;child.strictness=Strictness::kStrict;
    child.names={u"current"};child.parameter_scope={0};child.body_scope={1};child.capture_scope=ScopeId{2};child.body={0};
    child.scopes={{ScopeKind::kParameters,ScopeId{2}},{ScopeKind::kFunctionBody,ScopeId{0}},{ScopeKind::kCaptures,{}}};
    child.symbols={{{2},{0},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kSynthesized,{}}};
    child.captures={{{0},{tree.owner,{3}}}};child.origins={OriginalSite{{0},{0}}};
    child.expressions={{AssignmentExpr{SymbolTarget{{0}},ExprId{1}},{0}},{TreeLiteral{IntegerLiteral{0}},{0}}};
    child.statements={{BlockStmt{{1},0,1},{0}},{ExpressionStmt{ExprId{0}},{0}}};child.children={{1}};
    ConstructedFamily family;family.root={1};family.members={{std::move(child),{},{}},{tree,{},{}}};
    Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family,output)));
  }else Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(form==ScopedForm::kCallback)Check(output.str().find("scratch")==std::string::npos);
  if(form==ScopedForm::kCapturedCallback)Check(output.str().find("scratch")!=std::string::npos);
  if(emit)*fixture_output<<nlohmann::json{{"form",unsigned(form)},{"count",count},{"reverse",reverse},{"source",output.str()},{"work",bindings.work},{"bytes",bindings.allocated_bytes}}<<'\n';
  return bindings.work;
}
std::uint64_t DeclarationRenewal(unsigned count,bool reverse,bool emit) {
  Fixture f(Form::kStraight);auto& tree=f.tree;
  std::vector<StmtId> roots{f.Stmt(DeclarationStmt{{3},{}}),f.Stmt(DeclarationStmt{{4},{}}),
      f.Write(3,f.Read(0)),f.Stmt(ExpressionStmt{f.Call({f.Read(3)})})};
  for(unsigned i=0;i<count;++i)roots.push_back(f.Block({}));
  roots.push_back(f.Write(3,f.Call({f.Read(2)})));roots.push_back(f.Stmt(ReturnStmt{f.Read(3)}));
  f.Finish(roots,reverse);Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);
  if(const auto* error=std::get_if<TreeStructureError>(&planned))std::cerr<<"declaration renewal "<<unsigned(*error)<<'\n';
  Check(std::holds_alternative<BindingInitializers>(planned));
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);
  Check(std::holds_alternative<PrintedFunction>(printed));
  if(emit)*fixture_output<<nlohmann::json{{"form",96},{"renewal",true},{"count",count},{"reverse",reverse},{"source",output.str()}}<<'\n';
  return std::get<BindingInitializers>(planned).work;
}
int main(int argc,char** argv){
  if(argc==3&&std::string_view(argv[1])=="--form") {
    const auto form=std::stoul(argv[2]);Check(form<=25||(form>=38&&form<=43)||(form>=57&&form<=68)||form==96);
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      if(form<=unsigned(Form::kNativeProducer))Run(Form(form),count,reverse,true);
      else if(form==25)CanonicalScope(count,reverse,true);
      else if(form<=40)StorageProposal(form,count,reverse,true);
      else if(form<=43)StorageSnapshot(form,count,reverse,true);
      else if(form==96)DeclarationRenewal(count,reverse,true);
      else ComposedCopies(form,count,reverse,true);
    }
    return 0;
  }
  if(argc>1&&std::string_view(argv[1])=="--units"){
    for(unsigned form=0;form<=unsigned(ScopedForm::kCapturedCallback);++form)for(bool reverse:{false,true}){
      std::uint64_t previous=0;for(unsigned count:{0U,1U,200U,201U,400U,800U}){
        const auto work=ScopedCopies(ScopedForm(form),count,reverse,true);if(count==800)Check(work<previous*3);previous=work;}}return 0;
  }
  for(unsigned form=0;form<=unsigned(ScopedForm::kCapturedCallback);++form)for(bool reverse:{false,true})ScopedCopies(ScopedForm(form),0,reverse,false);
  if(argc>1&&std::string_view(argv[1])=="--chains"){
    for(bool conflict:{false,true})for(bool reverse:{false,true}){
      std::uint64_t previous=0;for(unsigned count:{1U,2U,200U,201U,400U,800U}){
        const auto work=Chain(count,conflict,reverse,true);if(count==800)Check(work<previous*3);previous=work;}}return 0;
  }
  if(argc>1&&std::string_view(argv[1])=="--scale"){
    std::uint64_t previous=0;for(unsigned count:{1U,200U,201U,400U,800U}){
      const auto work=Many(count,true);if(count==800)Check(work<previous*3);previous=work;}return 0;
  }
  const bool emit=argc>1;
  FixtureBatch batch;
  const auto geometric=[&](auto operation) {
    batch.Add([=] {
      std::uint64_t previous=0;
      for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
        const auto work=operation(count);if(count==800)Check(work<previous*3);previous=work;
      }
    });
  };
  for(unsigned form:{88U,89U,90U,91U,92U,93U,94U,95U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return TransitiveCopies(form,count,reverse,emit);});
  for(unsigned form:{77U,78U,79U,80U,81U,82U,83U,84U,85U,86U,87U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return AvailableCopies(form,count,reverse,emit);});
  for(unsigned form:{69U,70U,71U,72U,73U,74U,75U,76U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return ReadCopies(form,count,reverse,emit);});
  for(unsigned form:{57U,58U,59U,60U,61U,62U,63U,64U,65U,66U,67U,68U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return ComposedCopies(form,count,reverse,emit);});
  for(unsigned form:{53U,54U,55U,56U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return SharedCopies(form,count,reverse,emit);});
  for(unsigned form:{44U,45U,46U,47U,48U,49U,50U,51U,52U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return JoinedCopies(form,count,reverse,emit);});
  for(unsigned form:{41U,42U,43U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return StorageSnapshot(form,count,reverse,emit);});
  for(unsigned form:{38U,39U,40U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return StorageProposal(form,count,reverse,emit);});
  for(unsigned form:{34U,35U,36U,37U})for(bool reverse:{false,true})
    geometric([=](unsigned count){return RetiringCopies(form,count,reverse,emit);});
  for(bool loop:{false,true})for(bool inside:{false,true})for(bool reverse:{false,true})
    geometric([=](unsigned count){return NarrowedShadow(loop,inside,count,reverse,emit);});
  for(bool retired:{true,false})for(bool reverse:{false,true})
    geometric([=](unsigned count){return RetiredShadow(retired,count,reverse,emit);});
  for(bool observed:{true,false})for(bool reverse:{false,true})
    geometric([=](unsigned count){return NameScope(observed,count,reverse,emit);});
  for(bool reverse:{false,true})geometric([=](unsigned count){return CanonicalScope(count,reverse,emit);});
  for(bool conflict:{false,true})for(bool reverse:{false,true})batch.Add([=] {
    std::uint64_t previous=0;
    for(unsigned count:{1U,2U,200U,201U,400U,800U}) {
      const auto work=Chain(count,conflict,reverse,false);if(count==800)Check(work<previous*3);previous=work;
    }
  });
  for(unsigned form=0;form<=unsigned(Form::kNativeProducer);++form)batch.Add([=] {
    std::uint64_t previous=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=::Run(Form(form),count,reverse,emit);
      if(!reverse){if(count==800)Check(work<previous*3);previous=work;}
    }
  });
  batch.Add([=] {
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true})DeclarationRenewal(count,reverse,emit);
  });
  batch.Run(argc>1&&std::string_view(argv[1])=="--emit");
}
