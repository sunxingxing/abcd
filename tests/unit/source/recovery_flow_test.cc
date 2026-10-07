#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/binding_interference.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool condition,std::source_location at=std::source_location::current()) {
  if(!condition){std::cerr<<"recovery flow check "<<at.line()<<'\n';std::abort();}
}
void Link(RecoveryFlow& flow,unsigned from,unsigned to) {
  flow.edges.push_back({{from},{to},FlowCompletion::kNormal,flow.events[to].incoming,flow.events[from].outgoing});
  flow.events[to].incoming=flow.events[from].outgoing=flow.edges.size()-1;
}
// Independent small fixed-point oracle. Deliberately scans all edges and uses
// a tiny dense set; production uses sparse backward propagation and pair lookup.
bool Oracle(const RecoveryFlow& graph) {
  std::array<std::array<bool,2>,3> in{},out{};bool changed=true;
  while(changed){changed=false;
    for(unsigned point=0;point<3;++point) {
      std::array<bool,2> next{};
      for(const auto& edge:graph.edges)if(edge.from.value==point)
        for(unsigned binding=0;binding<2;++binding)next[binding]|=in[edge.to.value][binding];
      out[point]=next;
      const auto& event=graph.events[point];
      if(event.definition)next[graph.definitions[event.definition->value].binding.value]=false;
      if(event.read)next[event.read->value]=true;
      if(next!=in[point]){in[point]=next;changed=true;}
    }
  }
  for(const auto& definition:graph.definitions)for(unsigned binding=0;binding<2;++binding)
    if(binding!=definition.binding.value&&definition.copy!=SymbolId{binding}&&out[definition.point.value][binding])return true;
  return false;
}
void SmallGraphs() {
  constexpr unsigned actions=7,vertices=3,edge_bits=vertices*vertices;
  std::size_t cases=0;
  for(unsigned action_mask=0;action_mask<actions*actions*actions;++action_mask) {
    RecoveryFlow base{};base.events.resize(vertices);base.bindings.assign(2,BindingKind::kCompilerTemporary);
    unsigned encoded=action_mask;bool affinity=false;
    for(unsigned point=0;point<vertices;++point){const auto action=encoded%actions;encoded/=actions;
      if(action==1||action==2)base.events[point].read=SymbolId{action-1};
      if(action>=3){const unsigned target=(action-3)%2;std::optional<SymbolId> copy;
        if(action>=5){copy=SymbolId{1-target};base.events[point].read=copy;affinity=true;}
        base.events[point].definition=FlowDefinitionId{static_cast<std::uint32_t>(base.definitions.size())};
        base.definitions.push_back({{point},{target},FlowDefinitionKind::kAssignment,copy,{},{}});}
    }
    for(unsigned mask=0;mask<(1U<<edge_bits);++mask){auto graph=base;
      for(unsigned bit=0;bit<edge_bits;++bit)if(mask&(1U<<bit))Link(graph,bit/vertices,bit%vertices);
      const auto result=FindBindingInterference(graph);Check(std::holds_alternative<BindingInterference>(result));
      const auto& pairs=std::get<BindingInterference>(result).affinities;
      Check(pairs.size()==std::size_t(affinity));if(affinity)Check(pairs[0].interference==Oracle(graph));++cases;
    }
  }
  std::cout<<"independent small graphs "<<cases<<'\n';
}
void SourceEvents() {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.names={u"input",u"x",u"y"};tree.origins={OriginalSite{{0},{0}}};
  tree.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}},
    {{1},{1},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses},
    {{1},{2},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses}};
  tree.parameters={{{0},{},false}};
  tree.expressions={{SymbolExpr{{0}},{0}},{AssignmentExpr{SymbolTarget{{1}},{0}},{0}},
    {SymbolExpr{{1}},{0}},{AssignmentExpr{SymbolTarget{{2}},{2}},{0}},
    {UpdateExpr{{2},UpdateDirection::kIncrement,UpdateResult::kPrefix},{0}},{SymbolExpr{{2}},{0}}};
  tree.statements={{BlockStmt{{1},0,6},{0}},{DeclarationStmt{{1},{}},{0}},{DeclarationStmt{{2},{}},{0}},
    {ExpressionStmt{{1}},{0}},{ExpressionStmt{{3}},{0}},{ExpressionStmt{{4}},{0}},{ReturnStmt{ExprId{5}},{0}}};
  tree.children={{1},{2},{3},{4},{5},{6}};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<ExprId>> literals(tree.symbols.size()),replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> evaluations(tree.expressions.size(),ExpressionEvaluation::kRequired);
  const LegacyFlowProjection projection{aliases,literals,replacements,omitted,evaluations,normal};
  const auto built=BuildRecoveryFlow(tree,projection);Check(std::holds_alternative<RecoveryFlow>(built));
  const auto& flow=std::get<RecoveryFlow>(built);unsigned parameters=0,undefined=0,assignments=0,updates=0,scopes=0;
  std::optional<FlowPoint> read,convert,write;
  for(const auto& definition:flow.definitions) {
    parameters+=definition.kind==FlowDefinitionKind::kParameter;
    undefined+=definition.kind==FlowDefinitionKind::kUndefined;
    assignments+=definition.kind==FlowDefinitionKind::kAssignment;
    updates+=definition.kind==FlowDefinitionKind::kUpdate;
    if(definition.kind==FlowDefinitionKind::kUpdate)write=definition.point;
  }
  for(unsigned i=0;i<flow.events.size();++i){const auto& event=flow.events[i];
    scopes+=event.kind==FlowEventKind::kEnterScope||event.kind==FlowEventKind::kLeaveScope;
    if(event.expression==ExprId{4}){if(event.kind==FlowEventKind::kRead)read=FlowPoint{i};if(event.kind==FlowEventKind::kEvaluation)convert=FlowPoint{i};}}
  Check(parameters==1&&undefined==2&&assignments==2&&updates==1&&scopes==2);
  Check(read&&convert&&write);
  const auto has=[&](FlowPoint from,FlowPoint to,FlowCompletion completion){
    for(const auto& edge:flow.edges)if(edge.from==from&&edge.to==to&&edge.completion==completion)return true;return false;};
  Check(has(*read,*convert,FlowCompletion::kNormal));Check(has(*convert,*write,FlowCompletion::kNormal));
  Check(has(*convert,flow.terminal,FlowCompletion::kThrow));Check(!has(*convert,*write,FlowCompletion::kThrow));
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes-1,flow.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work-1}))==TreeStructureError::kWorkLimit);
  const auto rebuild=[&]() {
    replacements.resize(tree.expressions.size());normal.resize(tree.expressions.size());
    evaluations.resize(tree.expressions.size(),ExpressionEvaluation::kRequired);
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
    return std::get<RecoveryFlow>(BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,evaluations,normal}));
  };
  tree.expressions[5].expression=ConditionalExpr{{6},{7},{8}};
  tree.expressions.push_back({SymbolExpr{{0}},{0}});
  tree.expressions.push_back({TreeLiteral{IntegerLiteral{1}},{0}});
  tree.expressions.push_back({TreeLiteral{IntegerLiteral{2}},{0}});
  const auto selection=rebuild();
  std::optional<FlowPoint> branch;
  for(unsigned i=0;i<selection.events.size();++i)if(selection.events[i].kind==FlowEventKind::kTruthBranch)branch=FlowPoint{i};
  Check(branch.has_value());unsigned arms=0;
  for(const auto& edge:selection.edges)if(edge.from==*branch){
    if(edge.completion==FlowCompletion::kTaken){Check(edge.to==selection.expressions[7]);++arms;}
    if(edge.completion==FlowCompletion::kNotTaken){Check(edge.to==selection.expressions[8]);++arms;}}
  Check(arms==2);
  tree.expressions.resize(8);
  for(const auto operation:{TreeLogical::kAnd,TreeLogical::kOr,TreeLogical::kNullish}) {
    tree.expressions[5].expression=LogicalExpr{operation,{6},{7}};const auto logical=rebuild();
    unsigned right_edges=0;
    for(const auto& edge:logical.edges)if(edge.to==logical.expressions[7]) {
      Check(edge.completion==(operation==TreeLogical::kOr?FlowCompletion::kNotTaken:FlowCompletion::kTaken));
      Check(logical.events[edge.from.value].kind==(operation==TreeLogical::kNullish?FlowEventKind::kNullishBranch:FlowEventKind::kTruthBranch));++right_edges;}
    Check(right_edges==1);
  }
  tree.expressions.resize(7);
  for(bool generator:{false,true}) {
    tree.callable=generator?CallableKind::kGenerator:CallableKind::kAsyncFunction;
    tree.expressions[5].expression=generator?TreeExpression{YieldExpr{ExprId{6}}}:TreeExpression{AwaitExpr{{6}}};
    const auto paused=rebuild();unsigned next=0,thrown=0,returned=0;
    for(const auto& edge:paused.edges)if(paused.events[edge.from.value].kind==FlowEventKind::kSuspend){
      next+=edge.completion==FlowCompletion::kResumeNext;thrown+=edge.completion==FlowCompletion::kResumeThrow;
      returned+=edge.completion==FlowCompletion::kResumeReturn;}
    Check(next==1&&thrown==1&&returned==unsigned(generator));
  }
  tree.callable=CallableKind::kFunction;tree.expressions.resize(6);tree.expressions[5].expression=SymbolExpr{{2}};
  tree.statements[6].statement=ThrowStmt{{5}};const auto thrown=rebuild();unsigned explicit_throw=0;
  for(const auto& edge:thrown.edges)explicit_throw+=edge.completion==FlowCompletion::kThrow&&
      thrown.events[edge.from.value].kind==FlowEventKind::kControl&&edge.to==thrown.terminal;
  Check(explicit_throw==1);

}
}
int main(){SourceEvents();SmallGraphs();}
