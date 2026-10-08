#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/binding_interference.h"
#include "../../../src/source/storage_coalescing.h"
#include "../../../src/source/flow_definitions.h"
#include "../../../src/source/flow_constants.h"
#include "../../../src/source/definition_elision.h"
#include "../../../src/source/flow_dominance.h"
#include "../../../src/source/flow_motion.h"
#include <functional>
#include "../../../src/source/binding_initializer.h"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <tuple>
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
void UniversalObservations() {
  for(unsigned edges=0;edges<512;++edges)for(unsigned observed=0;observed<8;++observed) {
    RecoveryFlow flow{};flow.events.resize(3);
    for(unsigned i=0;i<3;++i)if(observed&(1U<<i)) {
      if(i==0)flow.events[i].read=SymbolId{0};
      else if(i==1)flow.events[i].definition=FlowDefinitionId{0};
      else flow.events[i].effect=ExpressionEvaluation::kRequired;
    }
    for(unsigned i=0;i<9;++i)if(edges&(1U<<i)) {
      Link(flow,i/3,i%3);flow.edges.back().completion=i%2?FlowCompletion::kThrow:FlowCompletion::kResumeNext;
    }
    const auto result=std::get<FlowMotion>(AnalyzeFlowMotion(flow));
    std::array<bool,3> reachable{};reachable[0]=true;
    for(unsigned round=0;round<3;++round)for(const auto& edge:flow.edges)
      if(reachable[edge.from.value])reachable[edge.to.value]=true;
    // Independent path enumeration stops at the first observation/sink.
    // Repeating an unobserved node is an infinite-path counterexample.
    std::function<std::optional<unsigned>(unsigned,unsigned)> walk;
    const auto successors=[&](unsigned node,unsigned path)->std::optional<unsigned> {
      std::optional<unsigned> answer;bool seen=false;
      for(const auto& edge:flow.edges)if(edge.from.value==node) {
        const auto next=walk(edge.to.value,path);if(!next)return {};
        if(seen&&answer!=next)return {};answer=next;seen=true;
      }
      return answer;
    };
    walk=[&](unsigned node,unsigned path)->std::optional<unsigned> {
      if((observed&(1U<<node)) || !flow.events[node].outgoing)return node;
      if(path&(1U<<node))return {};
      return successors(node,path|(1U<<node));
    };
    for(unsigned i=0;i<3;++i) {
      const auto expected=successors(i,0);
      Check(result.next[i]==(expected?std::optional{FlowPoint{*expected}}:std::nullopt));
    }
    const auto predecessors=[&](unsigned node,unsigned path)->std::optional<unsigned> {
      std::optional<unsigned> answer;bool seen=false;
      for(const auto& edge:flow.edges)if(edge.to.value==node && reachable[edge.from.value]) {
        const auto next=walk(edge.from.value,path);if(!next)return {};
        if(seen&&answer!=next)return {};answer=next;seen=true;
      }
      return answer;
    };
    walk=[&](unsigned node,unsigned path)->std::optional<unsigned> {
      if(node==0 || (observed&(1U<<node)))return node;
      if(path&(1U<<node))return {};
      return predecessors(node,path|(1U<<node));
    };
    for(unsigned i=0;i<3;++i) {
      const auto expected=reachable[i]?predecessors(i,0):std::nullopt;
      Check(result.previous[i]==(expected?std::optional{FlowPoint{*expected}}:std::nullopt));
    }
  }
  std::uint64_t previous=0;
  for(unsigned size:{200U,201U,400U,800U,1600U}) {
    RecoveryFlow flow{};flow.events.resize(size);
    for(unsigned i=1;i<size;++i)Link(flow,i-1,i);
    flow.events.back().effect=ExpressionEvaluation::kRequired;
    const auto result=std::get<FlowMotion>(AnalyzeFlowMotion(flow));
    for(unsigned i=0;i+1<size;++i)Check(result.next[i]==FlowPoint{size-1});
    for(unsigned i=1;i<size;++i)Check(result.previous[i]==FlowPoint{0});
    Check(!result.previous[0]);
    Check(!result.next.back());if(size>=400)Check(result.work<previous*3);previous=result.work;
    if(size==200) {
      Check(std::holds_alternative<FlowMotion>(AnalyzeFlowMotion(flow,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(AnalyzeFlowMotion(flow,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeFlowMotion(flow,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    }
    Link(flow,size/2,0);
    Check(!std::get<FlowMotion>(AnalyzeFlowMotion(flow)).next[0]);
  }
}
void CompletionDominance() {
  for(unsigned mask=0;mask<512;++mask) {
    RecoveryFlow flow{};flow.events.resize(3);flow.entry={0};
    for(unsigned i=0;i<9;++i)if(mask&(1U<<i)) {
      Link(flow,i/3,i%3);flow.edges.back().completion=i%2?FlowCompletion::kThrow:FlowCompletion::kResumeNext;
    }
    const auto reachable=[&](std::optional<unsigned> removed) {
      std::array<bool,3> seen{};std::vector<unsigned> pending;
      if(removed!=0){seen[0]=true;pending.push_back(0);}
      while(!pending.empty()) {
        const auto from=pending.back();pending.pop_back();
        for(const auto& edge:flow.edges)if(edge.from.value==from&&edge.to.value!=removed&&!seen[edge.to.value]) {
          seen[edge.to.value]=true;pending.push_back(edge.to.value);
        }
      }
      return seen;
    };
    const auto all=reachable({});const auto result=std::get<FlowDominance>(AnalyzeFlowDominance(flow));
    Check(result.tree.nodes.size()==flow.events.size());
    flow.statements.resize(3);
    for(unsigned a=0;a<3;++a) {
      flow.bindings.push_back({BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate,StmtId{a}});
      flow.definitions.push_back({{a},{a},FlowDefinitionKind::kInitializer,{},{},StmtId{a}});
    }
    const auto initialized=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
    for(unsigned a=0;a<3;++a) {
      const auto without=reachable(a);
      for(unsigned b=0;b<3;++b) {
        Check(result.Dominates({a},{b})==(all[b]&&!without[b]));
        Check(initialized.Completed({a},{b})==(!all[b]||!without[b]));
      }
    }
    Check(std::holds_alternative<FlowInitializations>(AnalyzeFlowInitializations(flow,{initialized.allocated_bytes,initialized.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowInitializations(flow,{initialized.allocated_bytes-1,initialized.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowInitializations(flow,{initialized.allocated_bytes,initialized.work-1}))==TreeStructureError::kWorkLimit);
    Check(std::holds_alternative<FlowDominance>(AnalyzeFlowDominance(flow,{result.allocated_bytes,result.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowDominance(flow,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowDominance(flow,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::cout<<"independent completion dominance graphs 512\n";
}
void RefinedInitializations() {
  // A conservative throw can bypass initialization. Proving the evaluation
  // inert removes that path; retaining bootstrap dominance loses the new fact.
  for(bool inert:{false,true})for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
    FunctionTree tree{};
    tree.expressions.push_back({inert?TreeExpression{SymbolExpr{{1}}}:TreeExpression{UnaryExpr{TreeUnary::kPlus,{1}}},{0}});
    tree.expressions.push_back({SymbolExpr{{1}},{0}});
    RecoveryFlow flow{};flow.entry={0};flow.events.resize(count+4);
    flow.bindings={{BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate,StmtId{0}},
        {BindingKind::kParameter}};
    const unsigned initialized=count+2,read=count+3;
    flow.definitions={{{initialized},{0},FlowDefinitionKind::kUndefined,{},{},StmtId{0}},
        {{0},{1},FlowDefinitionKind::kParameter}};
    flow.events[0].definition=FlowDefinitionId{1};
    flow.events[initialized].definition=FlowDefinitionId{0};
    flow.events[read].read=SymbolId{0};flow.events[1].expression=ExprId{0};
    for(unsigned i=0;i<read;++i)Link(flow,i,i+1);
    Link(flow,1,read);flow.edges.back().completion=FlowCompletion::kThrow;
    const auto bootstrap=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
    Check(!bootstrap.Completed({0},{read}));
    const std::array evaluations{inert?ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired,ExpressionEvaluation::kInert};
    const std::array<std::uint8_t,2> normal{0,0};
    Check(std::holds_alternative<FlowRefinement>(RefineRecoveryFlowEffects(flow,tree,evaluations,normal)));
    const auto current=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
    Check(current.Completed({0},{read})==inert);
    Check(!bootstrap.Completed({0},{read}));
  }
}
void PreservedInitializations() {
  for(bool bypass:{false,true}) {
    RecoveryFlow flow{};flow.entry={0};flow.events.resize(4);flow.statements.resize(2);
    flow.bindings={{BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate,StmtId{0}},
      {BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate,StmtId{1}}};
    flow.definitions={{{1},{0},FlowDefinitionKind::kUndefined,{},{},StmtId{0}},
      {{2},{1},FlowDefinitionKind::kUndefined,{},{},StmtId{1}}};
    flow.events[1].definition=FlowDefinitionId{0};flow.events[2].definition=FlowDefinitionId{1};flow.events[3].read=SymbolId{1};
    Link(flow,0,1);Link(flow,1,2);Link(flow,2,3);if(bypass){Link(flow,0,2);flow.edges.back().completion=FlowCompletion::kThrow;}
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
    const auto coverage=std::get<FlowReadCoverage>(AnalyzeFlowReadCoverage(flow,live));
    Check(coverage.definitions[1]);
    Check(std::holds_alternative<FlowReadCoverage>(AnalyzeFlowReadCoverage(flow,live,{coverage.allocated_bytes,coverage.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowReadCoverage(flow,live,{coverage.allocated_bytes-1,coverage.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowReadCoverage(flow,live,{coverage.allocated_bytes,coverage.work-1}))==TreeStructureError::kWorkLimit);
    const auto initialized=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
    const std::array<std::optional<SymbolId>,2> storage={std::nullopt,SymbolId{0}};
    const auto result=std::get<StorageInitializations>(PlanStorageInitializations(flow,live,initialized,storage));
    Check(bool(result.blocked[1])==bypass);Check(result.stores[1].has_value()==!bypass);
    Check(std::holds_alternative<StorageInitializations>(PlanStorageInitializations(flow,live,initialized,storage,{result.allocated_bytes,result.work})));
    Check(std::get<TreeStructureError>(PlanStorageInitializations(flow,live,initialized,storage,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanStorageInitializations(flow,live,initialized,storage,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
  }
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
      const auto& facts=std::get<BindingInterference>(result);const auto& pairs=facts.affinities;
      Check(pairs.size()==std::size_t(affinity));const auto expected=Oracle(graph);if(affinity)Check(pairs[0].interference==expected.interference);
      Check(facts.conflicts.size()==std::size_t(expected.interference));
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
      const auto coverage=std::get<FlowReadCoverage>(AnalyzeFlowReadCoverage(graph,live));
      for(unsigned d=0;d<graph.definitions.size();++d) {
        const auto binding=graph.definitions[d].binding;bool covered=!expected.live[graph.entry.value][binding.value];
        for(unsigned other=0;other<graph.definitions.size();++other)
          if(other!=d && graph.definitions[other].binding==binding && expected_live[other])covered=false;
        Check(bool(coverage.definitions[d])==covered);
      }
      const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(graph));
      const auto elisions=std::get<DefinitionElisions>(PlanDefinitionElisions(graph,demand));
      std::vector<DefinitionWrite> expected_elisions;
      for(unsigned d=0;d<graph.definitions.size();++d)if(!expected_live[d])expected_elisions.push_back({FlowReferenceSite{*graph.definitions[d].write},{}});
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
      std::vector<DefinitionWrite> writes;std::vector<StmtId> statements;
      for(unsigned i=0;i<3;++i){if(!needed[i])writes.push_back({FlowReferenceSite{ExprId{i}},{}});if(!retained[i])statements.push_back({i});}
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
void SourceScopeOracle() {
  // Independent lexical ancestry is the oracle; production only queries the
  // published interval index. Renumbering regions/statements changes no fact.
  const auto run=[](unsigned count,unsigned encoded,bool reverse) {
    std::vector<std::optional<unsigned>> parents(count);
    for(unsigned i=1;i<count;++i){parents[i]=encoded%i;encoded/=i;}
    const auto index=[&](unsigned i){return reverse?count-1-i:i;};
    RecoveryFlow flow{};flow.bindings.resize(count);flow.source_regions.resize(count);
    flow.source_occurrences.resize(count+1);std::size_t next=0;
    const auto visit=[&](auto&& self,unsigned node)->void {
      const auto first=SourcePosition{next++};flow.source_occurrences[index(node)]=first;
      for(unsigned child=node+1;child<count;++child)if(parents[child]==node)self(self,child);
      flow.source_regions[index(node)]={first,SourcePosition{next}};
    };
    visit(visit,0);
    for(unsigned binding=0;binding<count;++binding) {
      flow.bindings[index(binding)].scope=ProjectedBindingScope{SourceRegionId{index(binding)}};
      for(unsigned owner=0;owner<count;++owner) {
        bool expected=false;auto node=std::optional{owner};
        while(node){if(*node==binding){expected=true;break;}node=parents[*node];}
        Check(flow.BindingInScope({index(binding)},StmtId{index(owner)})==expected);
      }
      Check(!flow.BindingInScope({index(binding)},{}));
      Check(!flow.BindingInScope({index(binding)},StmtId{count}));
      flow.bindings[index(binding)].scope=UnmodeledBindingScope{};
      Check(!flow.BindingInScope({index(binding)},StmtId{index(binding)}));
      flow.bindings[index(binding)].scope=OriginalBindingScope{};
      Check(flow.BindingInScope({index(binding)},{}));
    }
  };
  for(unsigned encoded=0;encoded<24;++encoded)for(bool reverse:{false,true})run(5,encoded,reverse);
  // Large query sets keep production's constant-time index and exercise the
  // 200/201 boundary. The deliberately slow ancestry oracle remains test-only.
  for(unsigned count:{200U,201U,400U,800U})for(bool reverse:{false,true})run(count,1,reverse);
}
void InitialValueObligations() {
  for(bool needed:{false,true})for(bool private_binding:{false,true})
    for(bool inert:{false,true})for(bool required:{false,true})for(bool modeled:{false,true}) {
      RecoveryFlow flow{};FlowDemand demand;
      flow.bindings.push_back({BindingKind::kCompilerTemporary,
          private_binding?FlowBindingExposure::kPrivate:FlowBindingExposure::kObservable});
      flow.events.resize(1);flow.events[0].discarded=DiscardedId{0};
      flow.definitions.push_back({{0},{0},FlowDefinitionKind::kInitializer,{},ExprId{7},StmtId{2},{},
          inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable});
      flow.discarded.push_back({{2},required?ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert,
          modeled?std::optional{SymbolId{0}}:std::nullopt,ExprId{7}});
      demand.definitions={std::uint8_t(needed)};demand.retained={std::uint8_t(required)};
      const auto result=std::get<InitialValueElisions>(FindDeadInitialValues(flow,demand));
      const bool remove=!needed&&private_binding&&inert&&modeled;
      Check(result.values.size()==std::size_t(remove));
      if(remove)Check(result.values[0].evaluation==(required?std::optional{ExprId{7}}:std::nullopt));
      Check(std::holds_alternative<InitialValueElisions>(FindDeadInitialValues(flow,demand,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(FindDeadInitialValues(flow,demand,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(FindDeadInitialValues(flow,demand,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    }
  // Outlining a required RHS must survive both binding removal and later
  // dead-slot-write removal. Neither decision can erase its evaluation.
  RecoveryFlow flow{};flow.bindings={{BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate}};
  flow.events.resize(1);flow.events[0].definition=FlowDefinitionId{0};flow.events[0].discarded=DiscardedId{0};
  flow.discarded={{{2},ExpressionEvaluation::kRequired,SymbolId{0},ExprId{7}}};
  flow.definitions={{{0},{0},FlowDefinitionKind::kUndefined,{},{},StmtId{2},{},FlowWriteEffect::kInert}};
  FlowDemand demand;demand.definitions={0};demand.retained={1};
  const auto binding=std::get<BindingElisions>(FindUnusedBindings(flow,demand));
  Check(binding.declarations.size()==1&&binding.declarations[0].value==ExprId{7});
  flow.definitions[0].kind=FlowDefinitionKind::kUndefinedStore;flow.discarded[0].initializer.reset();
  const auto store=std::get<DefinitionElisions>(PlanDefinitionElisions(flow,demand));
  Check(store.writes.size()==1&&store.writes[0].value==ExprId{7}&&store.statements.empty());
}
void DefinitionSites() {
  // Cartesian semantic obligations: site representation cannot decide whether
  // a value write is removable. Completion/updates and observable writes stay.
  for(const auto kind:{FlowDefinitionKind::kParameter,FlowDefinitionKind::kUndefined,
      FlowDefinitionKind::kInitializer,FlowDefinitionKind::kAssignment,FlowDefinitionKind::kUpdate,
      FlowDefinitionKind::kCatch,FlowDefinitionKind::kUndefinedStore,FlowDefinitionKind::kFunctionSelf})
    for(bool statement:{false,true})for(bool needed:{false,true})for(bool inert:{false,true}) {
      RecoveryFlow flow{};FlowDemand demand;flow.events.resize(1);
      flow.definitions.push_back({{0},{0},kind,{},kind==FlowDefinitionKind::kUndefinedStore?std::nullopt:std::optional{ExprId{9}},
          statement?std::optional{StmtId{4}}:std::nullopt,
          statement?std::nullopt:std::optional{ExprId{3}},
          inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable});
      demand.definitions.push_back(needed);
      const auto result=std::get<DefinitionElisions>(PlanDefinitionElisions(flow,demand));
      const bool remove=!needed&&inert&&(kind==FlowDefinitionKind::kAssignment||kind==FlowDefinitionKind::kUndefinedStore);
      Check(result.writes.size()==std::size_t(remove));
      if(remove) {
        Check(result.writes[0].site==(statement?FlowReferenceSite{StmtId{4}}:FlowReferenceSite{ExprId{3}}));
        Check(result.writes[0].value==flow.definitions[0].value);
      }
      Check(std::holds_alternative<DefinitionElisions>(PlanDefinitionElisions(flow,demand,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(PlanDefinitionElisions(flow,demand,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(PlanDefinitionElisions(flow,demand,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    }
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
    const auto dominance=std::get<FlowDominance>(AnalyzeFlowDominance(graph));
    Check(dominance.tree.nodes.size()==size);
    if(!reverse){const auto work=live.work+definitions.work+dominance.work;
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
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> evaluations(tree.expressions.size(),ExpressionEvaluation::kRequired);
  const LegacyFlowProjection projection{aliases,literals,replacements,omitted,evaluations,normal};
  const auto built=BuildRecoveryFlow(tree,projection);Check(std::holds_alternative<RecoveryFlow>(built));
  const auto& flow=std::get<RecoveryFlow>(built);unsigned parameters=0,undefined=0,assignments=0,updates=0,scopes=0;
  Check(flow.value_uses==std::vector<FlowValueUse>{FlowValueUse::kObserved,FlowValueUse::kDiscarded,
      FlowValueUse::kObserved,FlowValueUse::kDiscarded,FlowValueUse::kDiscarded,FlowValueUse::kObserved});
  for(const auto& reference:flow.references) {Check(reference.exceptions.has_value());Check(reference.exceptions->thrown==flow.terminal);}
  const std::array<StmtId,6> owners{{{3},{3},{4},{4},{5},{6}}};
  for(const auto& reference:flow.references)
    Check(reference.owner==owners[std::get<ExprId>(reference.site).value]);
  // The same original read now executes in the return, not in its omitted
  // source store. Rebuilding must publish the destination, without mutating
  // the previous immutable result or confusing the read with its replacement.
  omitted[3]=1;replacements[5]=ExprId{0};
  const auto moved=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  unsigned forwarded=0;
  for(const auto& reference:moved.references)if(reference.site==FlowReferenceSite{ExprId{0}}) {
    Check(reference.owner==StmtId{6});++forwarded;
  }
  Check(forwarded==1);omitted[3]=0;replacements[5].reset();
  // Explicit free names retain their identity and follow the current owner,
  // independently of local binding references and reusable graph capacity.
  tree.names.push_back(u"free");tree.expressions[0].expression=GlobalNameExpr{{3}};
  auto named=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,evaluations,normal}));
  Check(named.names.size()==1&&named.names[0].name==NameId{3}&&named.names[0].site==ExprId{0}&&named.names[0].owner==StmtId{3});
  omitted[3]=1;replacements[5]=ExprId{0};
  const auto moved_name=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,evaluations,normal}));
  Check(moved_name.names.size()==1&&moved_name.names[0].owner==StmtId{6}&&moved_name.names[0].site==ExprId{0});
  Check(std::holds_alternative<FlowRefinement>(RefineRecoveryFlowEffects(named,tree,evaluations,normal)));
  Check(named.names.size()==1&&named.names[0].owner==StmtId{3});
  omitted[3]=0;replacements[5].reset();tree.expressions[0].expression=SymbolExpr{{0}};tree.names.pop_back();
  const auto cleared=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,evaluations,normal},{},std::move(named)));
  Check(cleared.names.empty());

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
  // Initialization proofs are obtained before removing impossible binding
  // throws. Refinement preserves semantic occurrences and coercion exceptions.
  const auto initialized_reads=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
  auto refined_evaluations=evaluations;auto refined_normal=normal;
  for(const auto& reference:flow.references)if(const auto* expression=std::get_if<ExprId>(&reference.site)) {
    if(!initialized_reads.Completed(reference.binding,reference.point))continue;
    if(std::holds_alternative<SymbolExpr>(tree.expressions[expression->value].expression))refined_evaluations[expression->value]=ExpressionEvaluation::kInert;
    if(std::holds_alternative<AssignmentExpr>(tree.expressions[expression->value].expression))refined_normal[expression->value]=1;
  }
  auto refined=flow;
  const auto refinement=std::get<FlowRefinement>(RefineRecoveryFlowEffects(refined,tree,refined_evaluations,refined_normal));
  Check(refined.events.size()==flow.events.size()&&refined.references.size()==flow.references.size());
  Check(refined.value_uses==flow.value_uses);
  for(std::size_t i=0;i<flow.references.size();++i)Check(refined.references[i].exceptions==flow.references[i].exceptions && refined.references[i].owner==flow.references[i].owner);
  Check(refined.edges.size()<flow.edges.size());
  const auto refined_initializations=std::get<FlowInitializations>(AnalyzeFlowInitializations(refined));
  bool coercion_throw=false;
  for(const auto& edge:refined.edges) {
    if(edge.from==*convert&&edge.completion==FlowCompletion::kThrow)coercion_throw=true;
    if(edge.completion==FlowCompletion::kThrow && refined.events[edge.from.value].expression==ExprId{0})Check(false);
  }
  Check(coercion_throw);
  Check(refined.events[convert->value].effect==ExpressionEvaluation::kRequired);
  bool inert_read=false;
  for(const auto& event:refined.events)if(event.expression==ExprId{0} && event.read) {
    Check(event.effect==ExpressionEvaluation::kInert);inert_read=true;
  }
  Check(inert_read);
  for(const auto& reference:refined.references)if(initialized_reads.Completed(reference.binding,reference.point))
    Check(refined_initializations.Completed(reference.binding,reference.point));
  for(unsigned boundary=0;boundary<3;++boundary) {
    auto copy=flow;const auto result=RefineRecoveryFlowEffects(copy,tree,refined_evaluations,refined_normal,
        {refinement.allocated_bytes-(boundary==1),refinement.work-(boundary==2)});
    if(!boundary)Check(std::holds_alternative<FlowRefinement>(result));
    else Check(std::get<TreeStructureError>(result)==(boundary==1?TreeStructureError::kResourceLimit:TreeStructureError::kWorkLimit));
  }
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
  Check(std::holds_alternative<UnmodeledBindingScope>(projected.bindings[1].scope));
  Check(!projected.BindingInScope({1},StmtId{3}));
  std::vector<SourceRegion> regions{{{0},{2}},{{1},{2}}};
  std::vector<std::optional<SourcePosition>> positions(tree.statements.size(),SourcePosition{0});positions[3]=SourcePosition{1};
  std::vector<SourceBindingScope> source_bindings(tree.symbols.size());source_bindings[1]=ProjectedBindingScope{{1}};
  auto scoped=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}}));
  Check(scoped.BindingInScope({1},StmtId{3})&&!scoped.BindingInScope({1},StmtId{4}));
  Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}},
      {scoped.allocated_bytes,scoped.work})));
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}},
      {scoped.allocated_bytes-1,scoped.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}},
      {scoped.allocated_bytes,scoped.work-1}))==TreeStructureError::kWorkLimit);
  source_bindings[1]=ProjectedBindingScope{{2}};
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}}))==TreeStructureError::kOwnership);
  const auto cleared_scopes=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings},{},std::move(scoped)));
  Check(cleared_scopes.source_regions.empty()&&cleared_scopes.source_occurrences.empty());
  Check(std::holds_alternative<UnmodeledBindingScope>(cleared_scopes.bindings[1].scope));
  // The native declaration remains authoritative when its value is pruned.
  // Its discarded RHS runs before lexical completion; throw never commits.
  std::vector<std::optional<DeclarationCompletion>> completions(tree.statements.size());
  completions[3]=DeclarationCompletion{{1},ExprId{0},DeclarationKind::kLet};
  const auto outlined=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},completions}));
  Check(outlined.bindings[1].declaration==StmtId{3}&&outlined.bindings[1].initialization==FlowDefinitionKind::kUndefined);
  Check(outlined.value_uses[0]==FlowValueUse::kDiscarded);
  const auto completion_order=std::get<FlowInitializations>(AnalyzeFlowInitializations(outlined));
  bool before=false,after=false;
  for(const auto& reference:outlined.references) {
    if(reference.site==FlowReferenceSite{ExprId{0}}) {
      Check(!completion_order.Completed({1},reference.point));before=true;
    }
    if(reference.site==FlowReferenceSite{ExprId{2}}) {
      Check(completion_order.Completed({1},reference.point));after=true;
    }
  }
  Check(before&&after);
  // Shared-slot initialization planning owns the recovered completion too,
  // independently of the original empty placeholder's statement identity.
  const auto outlined_live=std::get<FlowLiveness>(AnalyzeFlowLiveness(outlined));
  std::vector<std::optional<SymbolId>> outlined_storage(tree.symbols.size());outlined_storage[1]=SymbolId{0};
  const auto outlined_stores=std::get<StorageInitializations>(PlanStorageInitializations(
      outlined,outlined_live,completion_order,outlined_storage));
  Check(!outlined_stores.blocked[1]&&outlined_stores.stores[3]==SymbolId{0});
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
  // Rebuilding from prepaid buffers must retain no prior semantic identity.
  // Compare against a fresh graph after storage/declaration projection changes.
  const auto reused=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,storage},
      {},flow));
  Check(reused.allocated_bytes<shared.allocated_bytes);
  Check(reused.events.size()==shared.events.size()&&reused.edges.size()==shared.edges.size());
  Check(reused.definitions.size()==shared.definitions.size()&&reused.references.size()==shared.references.size());
  for(std::size_t i=0;i<shared.events.size();++i) {
    const auto& a=reused.events[i];const auto& b=shared.events[i];
    Check(std::tie(a.kind,a.read,a.definition,a.expression,a.scope,a.incoming,a.outgoing,a.discarded,a.effect)==
        std::tie(b.kind,b.read,b.definition,b.expression,b.scope,b.incoming,b.outgoing,b.discarded,b.effect));
  }
  for(std::size_t i=0;i<shared.edges.size();++i) {
    const auto& a=reused.edges[i];const auto& b=shared.edges[i];
    Check(std::tie(a.from,a.to,a.completion,a.incoming,a.outgoing)==std::tie(b.from,b.to,b.completion,b.incoming,b.outgoing));
  }
  for(std::size_t i=0;i<shared.definitions.size();++i) {
    const auto& a=reused.definitions[i];const auto& b=shared.definitions[i];
    Check(std::tie(a.point,a.binding,a.kind,a.copy,a.value,a.declaration,a.write,a.effect)==
        std::tie(b.point,b.binding,b.kind,b.copy,b.value,b.declaration,b.write,b.effect));
  }
  for(std::size_t i=0;i<shared.references.size();++i) {
    const auto& a=reused.references[i];const auto& b=shared.references[i];
    Check(std::tie(a.point,a.binding,a.kind,a.site,a.exceptions,a.owner)==std::tie(b.point,b.binding,b.kind,b.site,b.exceptions,b.owner));
  }
  Check(reused.value_uses==shared.value_uses);
  unsigned read_references=0,write_references=0;
  for(const auto& reference:shared.references) {
    Check(reference.binding!=SymbolId{2});
    if(reference.site==FlowReferenceSite{ExprId{4}}) {
      if(reference.kind==FlowReferenceKind::kRead)++read_references;else ++write_references;
    }
  }
  Check(read_references==1&&write_references==1);
  std::vector<std::optional<SymbolId>> undefined_stores(tree.statements.size());undefined_stores[2]=SymbolId{1};
  omitted[2]=0;
  const auto preserved=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,storage,undefined_stores}));
  unsigned resets=0;
  for(const auto& definition:preserved.definitions)if(definition.kind==FlowDefinitionKind::kUndefinedStore) {
    Check(definition.binding==SymbolId{1}&&definition.declaration==StmtId{2});++resets;
  }
  Check(resets==1&&preserved.bindings[1].declaration==StmtId{3});omitted[2]=1;
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
    auto result=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,evaluations,normal}));
    for(const auto& reference:result.references) {
      const auto expression=std::get<ExprId>(reference.site).value;
      Check(reference.owner==StmtId{expression<2?3U:expression<4?4U:expression==4?5U:6U});
    }
    return result;
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

namespace {
void GroupOracle(){
  constexpr unsigned bindings=4,pairs=bindings*(bindings-1)/2;
  unsigned variants=1;for(unsigned i=0;i<pairs;++i)variants*=4;
  for(unsigned encoded=0;encoded<variants;++encoded)for(unsigned policy=0;policy<5;++policy){
    BindingInterference facts;std::array<std::array<bool,bindings>,bindings> conflict{};
    unsigned remaining=encoded;
    for(unsigned a=0;a<bindings;++a)for(unsigned b=a+1;b<bindings;++b){
      const auto kind=remaining%4;remaining/=4;
      if(kind&1)facts.affinities.push_back({{a},{b},bool(kind&2)});
      if(kind&2){facts.conflicts.push_back({{a},{b}});conflict[a][b]=conflict[b][a]=true;}
    }
    std::array<StorageBinding,bindings> admission{};std::array<unsigned,bindings> owner{},preferred{};
    for(unsigned i=0;i<bindings;++i){owner[i]=preferred[i]=i;admission[i]={policy!=1||i!=0,policy!=2||i%2==0,policy==3?bindings-i:i,policy!=4||i%2==0};}
    const auto prefer=[&](unsigned a,unsigned b){const auto& left=admission[a];const auto& right=admission[b];
      if(left.unique_spelling!=right.unique_spelling)return left.unique_spelling?a:b;
      if(left.native_declaration!=right.native_declaration)return left.native_declaration?a:b;
      if(left.initialization_order!=right.initialization_order)return left.initialization_order<right.initialization_order?a:b;
      return std::min(a,b);};
    // Independent tiny dense oracle: inspect every cross-group member pair,
    // then rewrite all labels. No DSU, sparse adjacency or production helpers.
    for(const auto& affinity:facts.affinities){const auto a=affinity.first.value,b=affinity.second.value;
      if(!admission[a].eligible||!admission[b].eligible||owner[a]==owner[b])continue;
      const auto first=owner[a],second=owner[b];bool blocked=false;
      for(unsigned i=0;i<bindings;++i)for(unsigned j=0;j<bindings;++j)if(owner[i]==first&&owner[j]==second)blocked|=conflict[i][j];
      if(blocked)continue;const auto selected=prefer(preferred[first],preferred[second]);
      for(auto& label:owner)if(label==second)label=first;preferred[first]=selected;
    }
    const auto value=PlanStorageCoalescing(facts,admission);Check(std::holds_alternative<StorageCoalescing>(value));
    const auto& result=std::get<StorageCoalescing>(value);
    for(unsigned i=0;i<bindings;++i){const auto expected=preferred[owner[i]];
      Check(result.storage[i]==(expected==i?std::nullopt:std::optional{SymbolId{expected}}));}
    if(encoded==variants-1&&policy==0){
      Check(std::holds_alternative<StorageCoalescing>(PlanStorageCoalescing(facts,admission,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,admission,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,admission,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    }
  }
}
}
void DeclarationValues() {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.names={u"input",u"current",u"scratch"};tree.origins={OriginalSite{{0},{0}}};
  tree.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}},
      {{1},{1},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses},
      {{1},{2},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses}};
  tree.parameters={{{0},{},false}};
  tree.expressions={{SymbolExpr{{0}},{0}},{SymbolExpr{{1}},{0}},{SymbolExpr{{2}},{0}}};
  tree.statements={{BlockStmt{{1},0,3},{0}},{DeclarationStmt{{1},ExprId{0}},{0}},
      {DeclarationStmt{{2},ExprId{1}},{0}},{ReturnStmt{ExprId{2}},{0}}};tree.children={{1},{2},{3}};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  std::vector<std::optional<SymbolId>> aliases(3),storage(3);
  std::vector<std::optional<PrimitiveId>> literals(3);std::vector<std::optional<ExprId>> replacements(3);
  std::vector<std::uint8_t> omitted(4),normal(3),private_bindings{0,1,1};
  std::vector<ExpressionEvaluation> evaluations(3,ExpressionEvaluation::kRequired);
  std::vector<std::optional<BindingInitializer>> initializers(4);
  std::vector<std::optional<DeclarationValue>> values(4);
  const auto build=[&](RecoveryFlow capacity=RecoveryFlow{}) {
    return std::get<RecoveryFlow>(BuildRecoveryFlow(tree,{aliases,literals,replacements,omitted,
        evaluations,normal,initializers,private_bindings,storage,{},values},{},std::move(capacity)));
  };
  auto flow=build();unsigned definitions=0,writes=0;
  for(const auto& definition:flow.definitions)if(definition.kind==FlowDefinitionKind::kInitializer) {
    ++definitions;Check(definition.value&&definition.declaration&&!definition.write);
    Check(definition.copy==SymbolId{definition.binding.value-1});
  }
  for(const auto& reference:flow.references)if(reference.kind==FlowReferenceKind::kWrite) {
    ++writes;Check(std::get<StmtId>(reference.site)==reference.owner);
    Check(reference.exceptions&&reference.exceptions->thrown==flow.terminal);
  }
  Check(definitions==2&&writes==2);
  const auto initialized=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
  for(const auto& reference:flow.references) {
    Check(initialized.Completed(reference.binding,reference.point));
    if(reference.site==FlowReferenceSite{ExprId{1}})Check(!initialized.Completed({2},reference.point));
  }
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto interference=std::get<BindingInterference>(FindBindingInterference(flow,live));
  Check(!interference.affinities.empty());
  // A retained value write is an assignment, not a second initialization.
  values[2]=DeclarationValue{{1},SymbolId{1}};storage[2]=SymbolId{1};
  auto projected=build(std::move(flow));
  Check(projected.bindings[1].declaration==StmtId{1});
  Check(projected.value_uses[1]==FlowValueUse::kObserved);
  unsigned assignments=0;
  for(const auto& definition:projected.definitions)if(definition.kind==FlowDefinitionKind::kAssignment) {
    ++assignments;Check(definition.binding==SymbolId{1}&&definition.copy==SymbolId{1});
    Check(definition.effect==FlowWriteEffect::kInert&&definition.value==ExprId{1});
    Check(definition.WriteSite()==FlowReferenceSite{StmtId{2}});
  }
  Check(assignments==1);
  values[2]->target.reset();projected=build(std::move(projected));
  Check(projected.value_uses[1]==FlowValueUse::kDiscarded);
  for(const auto& definition:projected.definitions)Check(definition.kind!=FlowDefinitionKind::kAssignment);
  // Restoring the declaration with a self-read restores TDZ, even while buffer
  // capacity is reused. No definition or reference fact survives publication.
  values[2].reset();storage[2].reset();tree.expressions[1].expression=SymbolExpr{{2}};
  projected=build(std::move(projected));
  const auto tdz=std::get<FlowInitializations>(AnalyzeFlowInitializations(projected));
  bool checked=false;
  for(const auto& reference:projected.references)if(reference.site==FlowReferenceSite{ExprId{1}}) {
    Check(!tdz.Completed({2},reference.point));checked=true;
  }
  Check(checked);
}
void ConstantEquations() {
  // Independent backwards origin closure, deliberately slow and test-only.
  // It knows no SCC order and compares semantic integer seeds, not source IDs.
  for(unsigned mask=0;mask<512;++mask)for(unsigned encoded=0;encoded<64;++encoded) {
    RecoveryFlow flow{};flow.bindings.resize(3);FlowLiveness live{};
    std::vector<FlowDefinitionValue> values;std::array<unsigned,3> seeds{};auto code=encoded;
    const bool reverse=mask%2;const auto id=[&](unsigned i){return SymbolId{reverse?2-i:i};};
    const auto add=[&](unsigned target,FlowDefinitionValue value) {
      flow.definitions.push_back({{},id(target),FlowDefinitionKind::kAssignment});values.push_back(value);live.live_definitions.push_back(1);
    };
    for(unsigned i=0;i<3;++i) {
      flow.bindings[i].kind=BindingKind::kCompilerTemporary;flow.bindings[i].exposure=FlowBindingExposure::kPrivate;
      seeds[i]=code%4;code/=4;
      if(seeds[i]==3)add(i,UnknownFlowValue{});
      else if(seeds[i])add(i,FlowPrimitiveValue{IntegerLiteral{static_cast<std::int32_t>(seeds[i]-1)},PrimitiveId{i}});
    }
    for(unsigned source=0;source<3;++source)for(unsigned target=0;target<3;++target)
      if(mask&(1U<<(source*3+target)))add(target,FlowBindingValue{id(source)});
    const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));
    std::array<bool,3> grounded{};
    for(unsigned root=0;root<3;++root) {
      std::array<bool,3> seen{};std::vector<unsigned> pending{root};seen[root]=true;
      while(!pending.empty()) {
        const auto at=pending.back();pending.pop_back();grounded[root]|=seeds[at]==1||seeds[at]==2;
        for(unsigned source=0;source<3;++source)if((mask&(1U<<(source*3+at)))&&!seen[source]){seen[source]=true;pending.push_back(source);}
      }
    }
    for(unsigned root=0;root<3;++root) {
      std::array<bool,3> seen{};std::vector<unsigned> pending{root};seen[root]=true;
      bool unknown=false;std::optional<unsigned> expected;
      while(!pending.empty()) {
        const auto at=pending.back();pending.pop_back();
        if(seeds[at]==3||!grounded[at])unknown=true;
        else if(seeds[at]){if(expected&&*expected!=seeds[at]-1)unknown=true;else expected=seeds[at]-1;}
        for(unsigned source=0;source<3;++source)if((mask&(1U<<(source*3+at)))&&!seen[source]){seen[source]=true;pending.push_back(source);}
      }
      const auto& value=result.values[id(root).value];Check(bool(value)==(!unknown&&expected.has_value()));
      if(value)Check(std::get<IntegerLiteral>(value->value).value==static_cast<std::int32_t>(*expected));
    }
  }
  std::uint64_t small=0,large=0;
  for(unsigned count:{200U,201U,400U,800U}) {
    RecoveryFlow flow{};flow.bindings.resize(count);FlowLiveness live{};std::vector<FlowDefinitionValue> values;
    const auto add=[&](unsigned target,FlowDefinitionKind kind,FlowDefinitionValue value,bool needed=true) {
      flow.definitions.push_back({{1},{target},kind});values.push_back(value);live.live_definitions.push_back(needed);
    };
    for(unsigned i=0;i<count;++i) {
      flow.bindings[i].kind=BindingKind::kCompilerTemporary;flow.bindings[i].exposure=FlowBindingExposure::kPrivate;
      add(i,FlowDefinitionKind::kAssignment,FlowBindingValue{{(i+1)%count}});
    }
    add(0,FlowDefinitionKind::kInitializer,FlowPrimitiveValue{IntegerLiteral{42},PrimitiveId{0}});
    add(0,FlowDefinitionKind::kUndefined,FlowPrimitiveValue{UndefinedLiteral{},{}},false);
    auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));
    for(const auto& value:result.values)Check(value&&std::get<IntegerLiteral>(value->value).value==42);
    if(count==400)small=result.work;if(count==800)large=result.work;
    Check(std::holds_alternative<FlowConstants>(AnalyzeFlowConstants(flow,live,values,{result.allocated_bytes,result.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    const auto unknown=[&] {
      const auto analyzed=AnalyzeFlowConstants(flow,live,values);
      for(const auto& value:std::get<FlowConstants>(analyzed).values)Check(!value);
    };
    live.live_definitions.back()=1;
    unknown();
    live.live_definitions.back()=0;
    flow.bindings[0].exposure=FlowBindingExposure::kObservable;
    unknown();
    flow.bindings[0].exposure=FlowBindingExposure::kPrivate;live.live_in.push_back({flow.entry,{0}});
    unknown();
    live.live_in.clear();add(0,FlowDefinitionKind::kUpdate,UnknownFlowValue{},false);
    // An otherwise dead successful write still supplies normal-result values.
    unknown();
  }
  Check(large<small*3);std::cout<<"independent constant equations 32768\n";
}
int main(){ConstantEquations();SourceScopeOracle();InitialValueObligations();DefinitionSites();DeclarationValues();GroupOracle();RefinedInitializations();UniversalObservations();CompletionDominance();PreservedInitializations();SourceEvents();SmallGraphs();DemandGraphs();Scaling();}
