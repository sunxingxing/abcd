#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/binding_interference.h"
#include "../../../src/source/flow_definitions.h"
#include "../../../src/source/definition_elision.h"
#include "../../../src/source/binding_initializer.h"
#include <array>
#include <algorithm>
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
struct OracleFacts {bool interference=false;std::array<std::array<bool,2>,3> live{};};
OracleFacts Oracle(const RecoveryFlow& graph) {
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
    if(binding!=definition.binding.value&&definition.copy!=SymbolId{binding}&&out[definition.point.value][binding])return {true,in};
  return {false,in};
}
void SmallGraphs() {
  constexpr unsigned actions=7,vertices=3,edge_bits=vertices*vertices;
  std::size_t cases=0;
  for(unsigned action_mask=0;action_mask<actions*actions*actions;++action_mask) {
    RecoveryFlow base{};base.events.resize(vertices);base.bindings.assign(2,FlowBinding{BindingKind::kCompilerTemporary});
    unsigned encoded=action_mask;bool affinity=false;
    for(unsigned point=0;point<vertices;++point){const auto action=encoded%actions;encoded/=actions;
      if(action==1||action==2)base.events[point].read=SymbolId{action-1};
      if(action>=3){const unsigned target=(action-3)%2;std::optional<SymbolId> copy;
        if(action>=5){copy=SymbolId{1-target};base.events[point].read=copy;affinity=true;}
        base.events[point].definition=FlowDefinitionId{static_cast<std::uint32_t>(base.definitions.size())};
        base.definitions.push_back({{point},{target},FlowDefinitionKind::kAssignment,copy,{},{},ExprId{point},FlowWriteEffect::kInert});}
    }
    for(unsigned mask=0;mask<(1U<<edge_bits);++mask){auto graph=base;
      for(unsigned bit=0;bit<edge_bits;++bit)if(mask&(1U<<bit))Link(graph,bit/vertices,bit%vertices);
      const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(graph));
      const auto result=FindBindingInterference(graph,live);Check(std::holds_alternative<BindingInterference>(result));
      const auto& pairs=std::get<BindingInterference>(result).affinities;
      Check(pairs.size()==std::size_t(affinity));const auto expected=Oracle(graph);if(affinity)Check(pairs[0].interference==expected.interference);
      for(unsigned point=0;point<vertices;++point)for(unsigned binding=0;binding<2;++binding)
        Check(std::binary_search(live.live_in.begin(),live.live_in.end(),LiveBinding{{point},{binding}})==expected.live[point][binding]);
      // Independent forward fixed point without production liveness pruning.
      std::array<std::array<bool,3>,3> reaching{};bool changed=true;
      while(changed){changed=false;for(const auto& edge:graph.edges){
        auto outgoing=reaching[edge.from.value];const auto own=graph.events[edge.from.value].definition;
        if(own){for(unsigned d=0;d<graph.definitions.size();++d)
          if(graph.definitions[d].binding==graph.definitions[own->value].binding)outgoing[d]=false;outgoing[own->value]=true;}
        for(unsigned d=0;d<graph.definitions.size();++d)if(outgoing[d]&&!reaching[edge.to.value][d]){reaching[edge.to.value][d]=true;changed=true;}}}
      std::vector<ReachingDefinition> expected_reads;
      std::vector<std::uint8_t> expected_live(graph.definitions.size());
      for(unsigned point=0;point<vertices;++point)for(unsigned d=0;d<graph.definitions.size();++d)
        if(reaching[point][d]&&graph.events[point].read==graph.definitions[d].binding){expected_reads.push_back({{point},{d}});expected_live[d]=1;}
      const auto definitions=std::get<FlowDefinitions>(AnalyzeFlowDefinitions(graph,live));
      Check(definitions.reads==expected_reads);Check(live.live_definitions==expected_live);
      const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(graph));
      const auto elisions=std::get<DefinitionElisions>(PlanDefinitionElisions(graph,demand));
      std::vector<ExprId> expected_elisions;
      for(unsigned d=0;d<graph.definitions.size();++d)if(!expected_live[d])expected_elisions.push_back(*graph.definitions[d].write);
      Check(elisions.writes==expected_elisions);++cases;
    }
  }
  std::cout<<"independent small graphs "<<cases<<'\n';
}
void DemandGraphs() {
  std::size_t cases=0;
  for(unsigned layout=0;layout<3;++layout)for(unsigned edges=0;edges<512;++edges)
    for(unsigned effects=0;effects<8;++effects)for(unsigned external=0;external<8;++external) {
      RecoveryFlow graph{};graph.events.resize(15);
      std::array<unsigned,3> owner{};std::array<bool,3> needed{},retained{};
      for(unsigned i=0;i<3;++i) {
        owner[i]=layout==0?i:layout==1?0:i%2;
        graph.events[i].discarded=DiscardedId{owner[i]};graph.events[i].definition=FlowDefinitionId{i};

        graph.definitions.push_back({{i},{i},FlowDefinitionKind::kAssignment,{},{},{},ExprId{i},FlowWriteEffect::kInert});
        retained[i]=effects&(1U<<i);needed[i]=external&(1U<<i);
        graph.discarded.push_back({{i},retained[i]?ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert});
        if(needed[i]){graph.events[i+12].read=SymbolId{i};Link(graph,i,i+12);}
        for(unsigned j=0;j<3;++j) {
          const auto point=3+i*3+j;graph.events[point].discarded=DiscardedId{i};
          if(edges&(1U<<(i*3+j))){graph.events[point].read=SymbolId{j};Link(graph,j,point);}
        }
      }
      // Independent dense fixed point, intentionally unlike the production
      // adjacency/work queue. Includes cycles and many definitions per region.
      bool changed=true;
      while(changed) {
        const auto old_needed=needed,old_retained=retained;
        for(unsigned i=0;i<3;++i)if(needed[i])retained[owner[i]]=true;
        for(unsigned i=0;i<3;++i)if(retained[i])for(unsigned j=0;j<3;++j)
          if(edges&(1U<<(i*3+j)))needed[j]=true;
        changed=old_needed!=needed||old_retained!=retained;
      }
      const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(graph));
      for(unsigned i=0;i<3;++i)Check(bool(demand.definitions[i])==needed[i]&&bool(demand.retained[i])==retained[i]);
      const auto plan=std::get<DefinitionElisions>(PlanDefinitionElisions(graph,demand));
      std::vector<ExprId> writes;std::vector<StmtId> statements;
      for(unsigned i=0;i<3;++i){if(!needed[i])writes.push_back({i});if(!retained[i])statements.push_back({i});}
      Check(plan.writes==writes&&plan.statements==statements);
      if(edges==511&&effects==7&&external==7) {
        Check(std::holds_alternative<FlowDemand>(AnalyzeFlowDemand(graph,{demand.allocated_bytes,demand.work})));
        Check(std::get<TreeStructureError>(AnalyzeFlowDemand(graph,{demand.allocated_bytes-1,demand.work}))==TreeStructureError::kResourceLimit);
        Check(std::get<TreeStructureError>(AnalyzeFlowDemand(graph,{demand.allocated_bytes,demand.work-1}))==TreeStructureError::kWorkLimit);
        Check(std::holds_alternative<DefinitionElisions>(PlanDefinitionElisions(graph,demand,{plan.allocated_bytes,plan.work})));
        Check(std::get<TreeStructureError>(PlanDefinitionElisions(graph,demand,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
        Check(std::get<TreeStructureError>(PlanDefinitionElisions(graph,demand,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
      }
      ++cases;
    }
  std::cout<<"independent demand graphs "<<cases<<'\n';
}
void Scaling() {
  std::uint64_t previous=0;
  for(unsigned size:{200U,201U,400U,800U,1600U})for(bool reverse:{false,true}) {
    RecoveryFlow graph{};graph.events.resize(size);graph.bindings={{BindingKind::kCompilerTemporary}};
    const auto point=[&](unsigned i){return reverse?size-1-i:i;};
    std::vector<ReachingDefinition> expected;
    for(unsigned i=0;i<size;++i) {
      const auto at=point(i);
      if(i%2==0){graph.events[at].definition=FlowDefinitionId{static_cast<std::uint32_t>(graph.definitions.size())};
        graph.definitions.push_back({{at},{0},FlowDefinitionKind::kAssignment,{},{},{}});}
      else {graph.events[at].read=SymbolId{0};expected.push_back({{at},{i/2}});}
      if(i)Link(graph,point(i-1),at);
    }
    std::sort(expected.begin(),expected.end());
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(graph));
    const auto definitions=std::get<FlowDefinitions>(AnalyzeFlowDefinitions(graph,live));
    Check(definitions.reads==expected);
    Check(std::count(live.live_definitions.begin(),live.live_definitions.end(),1)==size/2);
    if(!reverse){const auto work=live.work+definitions.work;
      if(size>=800)Check(work<previous*3);previous=work;
      std::cout<<"flow facts size "<<size<<" work "<<work<<'\n';}
  }
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
  const auto liveness=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto definitions=std::get<FlowDefinitions>(AnalyzeFlowDefinitions(flow,liveness));
  for(unsigned i=0;i<flow.definitions.size();++i)
    Check(bool(liveness.live_definitions[i])==(flow.definitions[i].kind!=FlowDefinitionKind::kUndefined));
  Check(!definitions.reads.empty());
  Check(std::get<TreeStructureError>(AnalyzeFlowLiveness(flow,{liveness.allocated_bytes-1,liveness.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(AnalyzeFlowLiveness(flow,{liveness.allocated_bytes,liveness.work-1}))==TreeStructureError::kWorkLimit);
  Check(std::get<TreeStructureError>(AnalyzeFlowDefinitions(flow,liveness,{definitions.allocated_bytes-1,definitions.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(AnalyzeFlowDefinitions(flow,liveness,{definitions.allocated_bytes,definitions.work-1}))==TreeStructureError::kWorkLimit);
  // Projection declarations are initialization definitions, not assignments
  // that may disappear merely because their resulting value is dead.
  std::vector<std::optional<BindingInitializer>> native(tree.statements.size());
  native[3]=BindingInitializer{{1},{1},DeclarationKind::kLet};
  std::vector<std::uint8_t> private_bindings(tree.symbols.size(),1);
  const auto projected=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings}));
  bool initialized=false;
  for(const auto& definition:projected.definitions)if(definition.write==ExprId{1}) {
    Check(definition.kind==FlowDefinitionKind::kInitializer&&definition.declaration==StmtId{3});initialized=true;
  }
  Check(initialized);
  // Storage is a current semantic identity, not an immutable value copy.
  // Updating the removed slot must read and commit the retained binding.
  std::vector<std::optional<SymbolId>> storage(tree.symbols.size());storage[2]=SymbolId{1};
  omitted[2]=1;
  const auto shared=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,storage}));
  unsigned shared_updates=0,shared_reads=0;
  for(const auto& definition:shared.definitions) {
    Check(definition.binding!=SymbolId{2});
    if(definition.kind==FlowDefinitionKind::kUpdate){Check(definition.binding==SymbolId{1});++shared_updates;}
  }
  for(const auto& event:shared.events) {
    Check(event.read!=SymbolId{2});
    if(event.expression==ExprId{4}&&event.read){Check(event.read==SymbolId{1});++shared_reads;}
  }
  Check(shared_updates==1&&shared_reads==1);
  // A later read through the retained name demands a write through the removed
  // name. Using the pre-storage graph would falsely classify that write dead.
  const auto original_update=tree.expressions[4],original_return=tree.expressions[5];
  tree.expressions.push_back({TreeLiteral{IntegerLiteral{42}},{0}});
  tree.expressions[4].expression=AssignmentExpr{SymbolTarget{{2}},{6}};
  tree.expressions[5].expression=SymbolExpr{{1}};
  replacements.resize(tree.expressions.size());normal.resize(tree.expressions.size());normal[4]=1;
  evaluations.resize(tree.expressions.size(),ExpressionEvaluation::kRequired);
  const auto check_storage=[&](bool applied) {
    const auto current=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
        {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,
         applied?std::span<const std::optional<SymbolId>>(storage):std::span<const std::optional<SymbolId>>{}}));
    const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(current));bool found=false;
    for(unsigned i=0;i<current.definitions.size();++i)if(current.definitions[i].write==ExprId{4}) {
      Check(bool(demand.definitions[i])==applied);found=true;
    }
    Check(found);
  };
  check_storage(false);check_storage(true);
  tree.expressions.pop_back();tree.expressions[4]=original_update;tree.expressions[5]=original_return;
  replacements.resize(tree.expressions.size());normal.assign(tree.expressions.size(),0);
  evaluations.resize(tree.expressions.size());omitted[2]=0;
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
int main(){SourceEvents();SmallGraphs();DemandGraphs();Scaling();}
