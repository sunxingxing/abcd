#include "../../../src/source/source_variables.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/flow_motion.h"
#include "abcd/source/tree_printer.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <algorithm>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value,std::source_location at=std::source_location::current()) {if(!value){std::cerr<<"property check "<<at.line()<<'\n';std::abort();}}
struct Fixture {
  FunctionTree tree{};
  Fixture() {
    tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}},
      {ScopeKind::kBlock,ScopeId{1}},{ScopeKind::kCatch,ScopeId{1}}};
    tree.names={u"obj",u"key",u"rhs",u"state",u"error",u"fixed"};
    tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<3;++i) {
      tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
      tree.parameters.push_back({{i},{},false});
    }
    tree.symbols.push_back({{1},{3},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    tree.symbols.push_back({{3},{4},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression value) {ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;}
  ExprId Read(unsigned binding) {return Expr(SymbolExpr{{binding}});}
  StmtId Stmt(TreeStatement value) {StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;}
  StmtId Block(ScopeId scope,std::vector<StmtId> children) {
    const auto first=static_cast<unsigned>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return Stmt(BlockStmt{scope,first,static_cast<unsigned>(children.size())});
  }
  void Finish(std::vector<StmtId> children) {
    auto& root=std::get<BlockStmt>(tree.statements[0].statement);root.first_child=tree.children.size();root.child_count=children.size();
    tree.children.insert(tree.children.end(),children.begin(),children.end());
    for(unsigned i=0;i<tree.statements.size();++i)if(const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement))
      for(unsigned j=0;j<=block->child_count;++j) {
        const auto anchor=static_cast<unsigned>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},j},{0}});
        tree.operations.push_back({{anchor},AnchorId{anchor}});
      }
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  }
};
void PropertyState(bool emit,bool computed,bool read,bool reverse) {
  Fixture f;
  const auto initial=f.Stmt(DeclarationStmt{{3},f.Expr(TreeLiteral{IntegerLiteral{7}})});
  auto base=f.Read(0),key=f.Read(1);
  // Named access has no computed key child. Keep ownership exhaustive.
  if(!computed)f.tree.expressions.pop_back();
  const MemberKey member=computed?MemberKey{ComputedKey{key}}:MemberKey{NameId{5}};
  const auto rhs=f.Expr(AssignmentExpr{SymbolTarget{{3}},f.Read(2)});
  auto access=read?f.Expr(MemberExpr{base,member}):f.Expr(AssignmentExpr{MemberTarget{base,member},rhs});
  // Reading still tests a later independent RHS write, after the access.
  std::vector<StmtId> inside{f.Stmt(ExpressionStmt{access})};
  if(read)inside.push_back(f.Stmt(ExpressionStmt{rhs}));
  const auto body=f.Block({2},inside),handler=f.Block({3},{f.Stmt(ReturnStmt{f.Read(3)})});
  const auto attempt=f.Stmt(TryStmt{body,CatchClause{{4},handler},{}});
  f.Finish({initial,attempt,f.Stmt(ReturnStmt{f.Read(3)})});
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(f.tree.expressions.size()-1-id.value)};};
    for(auto& item:f.tree.expressions)std::visit([&](auto& node) {
      using T=std::decay_t<decltype(node)>;
      const auto remap=[&](auto& target){target.base=map(target.base);if(auto* key=std::get_if<ComputedKey>(&target.key))key->expression=map(key->expression);};
      if constexpr(std::is_same_v<T,MemberExpr>)remap(node);
      else if constexpr(std::is_same_v<T,AssignmentExpr>) {node.value=map(node.value);if(auto* target=std::get_if<MemberTarget>(&node.target))remap(*target);}
      else Check(std::is_same_v<T,TreeLiteral>||std::is_same_v<T,SymbolExpr>);
    },item.expression);
    for(auto& item:f.tree.statements)std::visit([&](auto& node) {
      using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=map(*node.initializer);}
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=map(*node.value);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=map(node.expression);
    },item.statement);
    base=map(base);key=map(key);access=map(access);std::reverse(f.tree.expressions.begin(),f.tree.expressions.end());
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(f.tree)));
  }
  std::vector<std::optional<SymbolId>> aliases(f.tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(f.tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(f.tree.expressions.size());
  std::vector<std::uint8_t> omitted(f.tree.statements.size()),normal(f.tree.expressions.size());
  std::vector<ExpressionEvaluation> evaluations(f.tree.expressions.size(),ExpressionEvaluation::kRequired);
  for(unsigned i=0;i<f.tree.expressions.size();++i)
    if(std::holds_alternative<SymbolExpr>(f.tree.expressions[i].expression))normal[i]=1;
    else if(const auto* assignment=std::get_if<AssignmentExpr>(&f.tree.expressions[i].expression))
      if(std::holds_alternative<SymbolTarget>(assignment->target))normal[i]=1;
  const LegacyFlowProjection projection{aliases,literals,replacements,omitted,evaluations,normal};
  auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(f.tree,projection));
  Check(flow.properties.size()==1);
  const auto& property=flow.properties[0];Check(property.site==access&&property.base==base);
  Check(flow.actions.empty()); // No unused raw-phase bridge on source-only input.
  // A model adapter supplies phase identities explicitly. Receiver checking
  // begins after operand/RHS evaluation; acquisition cannot stand in for it.
  OriginalFlow raw{};raw.identity=f.tree.identity;raw.method=f.tree.method;
  raw.value_revision=f.tree.value_revision;raw.cfg_revision=f.tree.cfg_revision;
  raw.statements=f.tree.statements.size();raw.bindings=f.tree.symbols.size();
  raw.input_ranges.resize(f.tree.expressions.size());
  raw.phases={{{0},{0},analysis::ExceptionPhase::kBefore,analysis::OpId{0}},
      {{1},{0},analysis::ExceptionPhase::kCommit,analysis::OpId{0}},
      {{2},{1},analysis::ExceptionPhase::kCatch,{}}};
  raw.values.push_back({analysis::CatchDefinition{{2}},analysis::ValueKinds::Language()});
  raw.catches.resize(f.tree.symbols.size());raw.catches[4]=OriginalCatchBinding{{0},{2},handler};
  raw.exceptional_exits.push_back({analysis::OpId{0},{0},analysis::LanguageEffect::kUserCode,
      analysis::ValueHandler{{{0},{0},{0}},{2}}});
  OriginalOperation operation{};operation.life=analysis::ValueLife::kLive;
  operation.normal=operation.before_throw=analysis::LanguageEffect::kUserCode|analysis::LanguageEffect::kHeapRead|analysis::LanguageEffect::kHeapWrite;
  operation.exceptional=analysis::ExceptionalCompletion::kMayThrow;
  operation.expression=operation.intrinsic_expression=access;raw.operations.push_back(operation);
  auto phased_projection=projection;phased_projection.original=&raw;
  const auto phased=std::get<RecoveryFlow>(BuildRecoveryFlow(f.tree,phased_projection));
  const auto& native=*phased.actions[access.value];const auto& reference=phased.properties[0];
  Check(native.before==reference.receiver&&native.before!=reference.acquisition);
  Check(native.normal==reference.normal&&native.exceptions==reference.exceptions);
  Check(FindOriginalPhase(phased,{0},analysis::ExceptionPhase::kBefore,analysis::OpId{0})==native.before);
  Check(FindOriginalPhase(phased,{1},analysis::ExceptionPhase::kCommit,analysis::OpId{0})==native.normal);
  Check(phased.original_exceptions.size()==1&&phased.original_exceptions[0]);
  const auto& route=std::get<FlowOriginalException>(*phased.original_exceptions[0]);
  Check(route.before==reference.receiver&&route.thrown==reference.exceptions.thrown);
  auto escaping=raw;escaping.exceptional_exits[0].handler.reset();
  auto escaping_projection=projection;escaping_projection.original=&escaping;
  const auto mismatch=std::get<RecoveryFlow>(BuildRecoveryFlow(f.tree,escaping_projection));
  Check(!mismatch.original_exceptions[0]); // Raw escape cannot certify a native catch route.
  const auto source_only=std::get<RecoveryFlow>(BuildRecoveryFlow(f.tree,projection,{},phased));
  Check(source_only.actions.empty()&&source_only.actions.capacity()==0);
  Check(source_only.original_phases.empty()&&source_only.original_phases.capacity()==0);
  Check(source_only.original_exceptions.empty()&&source_only.original_exceptions.capacity()==0);
  Check(source_only.original_handlers.empty()&&source_only.original_handlers.capacity()==0);
  Check(property.kind==(read?FlowReferenceKind::kRead:FlowReferenceKind::kWrite));
  Check(flow.events[property.acquisition.value].kind==FlowEventKind::kPropertyReference);
  Check(flow.events[property.access.value].property==FlowPropertySite{FlowPropertyId{0},FlowPropertyPhase::kAccess});
  Check(flow.events[property.acquisition.value].effect==ExpressionEvaluation::kInert);
  Check(property.exceptions.thrown!=flow.terminal&&property.owner==inside[0]);
  const auto isThrow=[&](const auto& graph){return std::any_of(graph.edges.begin(),graph.edges.end(),[&](const auto& edge){return edge.from==property.key_conversion&&edge.to==property.exceptions.thrown&&edge.completion==FlowCompletion::kThrow;});};
  Check(isThrow(flow)==computed);
  Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(f.tree,projection,{flow.allocated_bytes,flow.work})));
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(f.tree,projection,{flow.allocated_bytes-1,flow.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(f.tree,projection,{flow.allocated_bytes,flow.work-1}))==TreeStructureError::kWorkLimit);
  if(computed&&!read) {
    const auto motion=std::get<FlowMotion>(AnalyzeFlowMotion(flow));
    const auto reference=std::find_if(flow.references.begin(),flow.references.end(),[&](const auto& ref){return ref.site==FlowReferenceSite{key};});
    Check(reference!=flow.references.end());
    const auto next=motion.next[reference->point.value];
    // Acquisition is not an observation or throwing check. The RHS GetValue
    // comes next; the retained key converts only after the RHS write.
    Check(next&&flow.events[next->value].kind==FlowEventKind::kRead);
    Check(flow.events[next->value].expression!=access);
    const auto phase=motion.next[property.receiver.value];
    Check(!phase); // Receiver failure bypasses the key; normal-edge order is separate.
    Check(std::any_of(flow.edges.begin(),flow.edges.end(),[&](const auto& edge){return edge.from==property.receiver&&edge.to==property.key_conversion&&edge.completion==FlowCompletion::kNormal;}));
  }
  const auto refined=RefineRecoveryFlowEffects(flow,f.tree,evaluations,normal);Check(std::holds_alternative<FlowRefinement>(refined));
  Check(isThrow(flow)==computed);
  const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(flow));
  bool retained=false;for(unsigned i=0;i<flow.definitions.size();++i)if(flow.definitions[i].declaration==initial)retained=demand.definitions[i];
  Check(retained==read);
  if(computed&&!read) {
    auto mutant=flow;mutant.edges.clear();for(auto& event:mutant.events){event.incoming.reset();event.outgoing.reset();}
    for(auto edge:flow.edges) {
      edge.incoming=mutant.events[edge.to.value].incoming;edge.outgoing=mutant.events[edge.from.value].outgoing;
      const auto id=mutant.edges.size();mutant.edges.push_back(edge);mutant.events[edge.to.value].incoming=id;mutant.events[edge.from.value].outgoing=id;
    }
    const auto from=property.acquisition,to=property.exceptions.thrown;
    const auto id=mutant.edges.size();mutant.edges.push_back({from,to,FlowCompletion::kThrow,mutant.events[to.value].incoming,mutant.events[from.value].outgoing});
    mutant.events[to.value].incoming=id;mutant.events[from.value].outgoing=id;
    const auto wrong=std::get<FlowDemand>(AnalyzeFlowDemand(mutant));
    for(unsigned i=0;i<mutant.definitions.size();++i)if(mutant.definitions[i].declaration==initial)Check(wrong.definitions[i]);
  }
  if(computed)for(const auto domain:{analysis::ValueKinds::Language(),analysis::ValueKinds::String(),analysis::ValueKinds::Number(),analysis::ValueKinds::Boolean()}) {
    std::vector<analysis::ValueKinds> domains(f.tree.expressions.size(),analysis::ValueKinds::Language());domains[key.value]=domain;
    auto projected=projection;projected.normal_kinds=domains;
    const auto known=std::get<RecoveryFlow>(BuildRecoveryFlow(f.tree,projected));
    const bool required=!domain.IsString()&&(ResourceFailures(f.tree.identity.contract)==ResourceFailureScope::kObservable||!CoercionPrimitive(domain));
    Check((known.events[known.properties[0].key_conversion.value].effect==ExpressionEvaluation::kRequired)==required);
    Check(isThrow(known)==required);
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(f.tree,output)));
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"computed",computed},{"read",read},{"reverse",reverse}}<<'\n';
}
void PropertyScaling() {
  std::uint64_t previous_work=0;std::size_t previous_bytes=0;
  for(unsigned count:{200U,201U,400U,800U}) {
    Fixture f;std::vector<StmtId> body;const auto initial=f.Stmt(DeclarationStmt{{3},{}});
    for(unsigned i=0;i<count;++i)body.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{
      MemberTarget{f.Read(0),ComputedKey{f.Read(1)}},f.Read(2)})}));
    f.Finish({initial,f.Stmt(TryStmt{f.Block({2},body),CatchClause{{4},f.Block({3},{f.Stmt(ReturnStmt{f.Read(2)})})},{}})});
    std::vector<std::optional<SymbolId>> aliases(f.tree.symbols.size());std::vector<std::optional<PrimitiveId>> literals(f.tree.symbols.size());
    std::vector<std::optional<ExprId>> replacements(f.tree.expressions.size());std::vector<std::uint8_t> omitted(f.tree.statements.size()),normal(f.tree.expressions.size());
    std::vector<ExpressionEvaluation> evaluations(f.tree.expressions.size(),ExpressionEvaluation::kRequired);
    const LegacyFlowProjection projection{aliases,literals,replacements,omitted,evaluations,normal};
    const auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(f.tree,projection));Check(flow.properties.size()==count);
    if(previous_work) {
      const double ratio=count==201?1.1:2.5;
      Check(flow.work<=previous_work*ratio&&flow.allocated_bytes<=previous_bytes*ratio);
    }
    previous_work=flow.work;previous_bytes=flow.allocated_bytes;
  }
}
}
int main(int argc,char**) {for(bool computed:{false,true})for(bool read:{false,true})for(bool reverse:{false,true})PropertyState(argc>1,computed,read,reverse);PropertyScaling();}
