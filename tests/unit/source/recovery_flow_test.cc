#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/binding_interference.h"
#include "../../../src/source/storage_coalescing.h"
#include "../../../src/source/flow_definitions.h"
#include "../../../src/source/flow_constants.h"
#include "../../../src/source/flow_copies.h"
#include "../../../src/source/recovery_schedule.h"
#include "../../../src/source/source_names.h"
#include "../../../src/source/source_name_retirement.h"
#include <bit>
#include <limits>
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
      for(bool projected:{false,true}) {
        const SourceRegionId region{index(binding)};
        flow.bindings[index(binding)].scope=projected?SourceBindingScope{ProjectedBindingScope{region}}:
            SourceBindingScope{OriginalBindingScope{region}};
        Check(flow.BindingRegion({index(binding)})==region);
        for(unsigned owner=0;owner<count;++owner) {
          bool expected=false;auto node=std::optional{owner};
          while(node){if(*node==binding){expected=true;break;}node=parents[*node];}
          Check(flow.BindingInScope({index(binding)},StmtId{index(owner)})==expected);
        }
        Check(!flow.BindingInScope({index(binding)},{}));
        Check(!flow.BindingInScope({index(binding)},StmtId{count}));
      }
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
void BindingNameOracle() {
  std::uint64_t cases=0;
  const auto run=[&](unsigned count,unsigned encoded,unsigned mask,unsigned reason,bool reverse,bool star) {
    const auto id=[&](unsigned i){return SymbolId{reverse?count-1-i:i};};
    RecoveryFlow flow{};flow.bindings.resize(count);flow.source_regions={{{0},{1}}};flow.source_occurrences={SourcePosition{0}};
    std::vector<std::optional<unsigned>> parents(count);
    std::vector<SourceBindingShadow> canonical;
    std::vector<bool> retained(count);
    for(unsigned i=0;i<count;++i) {
      auto& binding=flow.bindings[id(i).value];binding.kind=BindingKind::kCompilerTemporary;binding.exposure=FlowBindingExposure::kPrivate;
      retained[i]=count<=5?bool(mask&(1U<<i)):i%3==0;
      if(retained[i])switch(reason) {
        case 0:flow.references.push_back({{},id(i),FlowReferenceKind::kRead,ExprId{i}});break;
        case 1:flow.definitions.push_back({{},id(i),FlowDefinitionKind::kUndefined});break;
        case 2:binding.exposure=FlowBindingExposure::kObservable;break;
        case 3:binding.kind=BindingKind::kLocal;break;
        case 4:binding.kind=BindingKind::kCapture;break;
      }
      if(i){parents[i]=count<=5?encoded%i:star?0:i-1;encoded/=i;canonical.push_back({id(*parents[i]),SourceRegionId{0},id(i)});}
    }
    if(reverse)std::reverse(canonical.begin(),canonical.end());
    const auto names=std::get<FlowBindingNames>(AnalyzeFlowBindingNames(flow,canonical));
    for(unsigned i=0;i<count;++i)Check((names.presence[id(i).value]==SourceBindingPresence::kRetained)==retained[i]);
    std::vector<std::pair<SymbolId,SymbolId>> expected,actual;
    // Deliberately slow independent ancestor walk is confined to the oracle.
    for(unsigned i=1;i<count;++i)if(retained[i]) {
      auto parent=parents[i];while(parent&&!retained[*parent])parent=parents[*parent];
      if(parent)expected.emplace_back(id(*parent),id(i));
    }
    for(const auto& shadow:names.shadows){Check(shadow.region==SourceRegionId{0});Check(shadow.shadowing.has_value());actual.emplace_back(shadow.binding,*shadow.shadowing);}
    std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());Check(actual==expected);
    if(count>=200||(!encoded&&!mask&&!reverse)) {
      Check(std::holds_alternative<FlowBindingNames>(AnalyzeFlowBindingNames(flow,canonical,{names.allocated_bytes,names.work})));
      Check(std::get<TreeStructureError>(AnalyzeFlowBindingNames(flow,canonical,{names.allocated_bytes-1,names.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeFlowBindingNames(flow,canonical,{names.allocated_bytes,names.work-1}))==TreeStructureError::kWorkLimit);
    }
    ++cases;return names.work;
  };
  for(unsigned encoded=0;encoded<24;++encoded)for(unsigned mask=0;mask<32;++mask)
    for(unsigned reason=0;reason<5;++reason)for(bool reverse:{false,true})run(5,encoded,mask,reason,reverse,false);
  for(bool star:{false,true})for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{200U,201U,400U,800U}){const auto work=run(count,0,0,0,reverse,star);if(count==800)Check(work<previous*3);previous=work;}
  }
  RecoveryFlow flow{};flow.bindings.resize(3);for(auto& binding:flow.bindings){binding.kind=BindingKind::kCompilerTemporary;binding.exposure=FlowBindingExposure::kPrivate;}
  flow.source_regions={{{0},{1}}};flow.source_occurrences={SourcePosition{0}};
  const auto rejected=[&](std::vector<SourceBindingShadow> shadows){Check(std::get<TreeStructureError>(AnalyzeFlowBindingNames(flow,shadows))==TreeStructureError::kOwnership);};
  rejected({{{0},{},SymbolId{1}},{{2},{},SymbolId{1}}}); // Two claimed nearest parents.
  rejected({{{0},{},SymbolId{1}},{{1},{},SymbolId{0}}}); // Cyclic provenance.
  rejected({{{0},{},SymbolId{3}}});rejected({{{0},{},SymbolId{0}}});rejected({{{0},SourceRegionId{1},SymbolId{1}}});
  const std::vector<SourceBindingShadow> legacy{{{0},SourceRegionId{0},{}}};
  Check(std::get<FlowBindingNames>(AnalyzeFlowBindingNames(flow,legacy)).shadows.size()==1);
  for(const auto kind:{BindingKind::kParameter,BindingKind::kLocal,BindingKind::kCatchParameter,
      BindingKind::kFunctionSelf,BindingKind::kCapture,BindingKind::kModule}) {
    flow.bindings[0].kind=kind;Check(std::get<FlowBindingNames>(AnalyzeFlowBindingNames(flow,{})).presence[0]==SourceBindingPresence::kRetained);
  }
  flow.bindings[0].kind=BindingKind::kCompilerTemporary;
  flow.references.push_back({{},{3},FlowReferenceKind::kRead,ExprId{0}});
  Check(std::get<TreeStructureError>(AnalyzeFlowBindingNames(flow,{}))==TreeStructureError::kOwnership);
  std::cout<<"independent binding name cases "<<cases<<'\n';
}
void SourceNameOracle() {
  std::uint64_t cases=0;
  const auto run=[&](unsigned count,unsigned encoded,unsigned projected,unsigned mask,bool reverse,bool star) {
    const auto id=[&](unsigned i){return SymbolId{reverse?count-1-i:i};};
    std::vector<std::optional<unsigned>> parents(count);std::vector<unsigned> scope(count);
    for(unsigned i=1;i<count;++i){parents[i]=count<=5?encoded%i:star?0:i-1;encoded/=i;}
    const auto contains=[&](unsigned parent,unsigned child){auto node=std::optional{child};while(node){if(*node==parent)return true;node=parents[*node];}return false;};
    RecoveryFlow flow{};flow.bindings.resize(count);flow.source_regions.resize(count);flow.source_occurrences.resize(count);
    flow.source_spellings.resize(count,SourceSpellingId{0});flow.source_name_spellings={SourceSpellingId{0},std::nullopt};
    std::vector<SourceBindingPresence> presence(count);std::vector<SourceBindingShadow> canonical;std::size_t position=0;
    const auto visit=[&](auto&& self,unsigned node)->void {
      const SourcePosition first{position++};flow.source_occurrences[id(node).value]=first;
      for(unsigned child=node+1;child<count;++child)if(parents[child]==node)self(self,child);
      flow.source_regions[id(node).value]={first,SourcePosition{position}};
    };visit(visit,0);
    bool ambiguous=false;
    for(unsigned i=0;i<count;++i) {
      scope[i]=i;
      if(count<=5&&(projected&(1U<<i))&&i+1<count&&contains(i,i+1))scope[i]=i+1;
      flow.bindings[id(i).value].scope=scope[i]==i?SourceBindingScope{OriginalBindingScope{SourceRegionId{id(scope[i]).value}}}:
          SourceBindingScope{ProjectedBindingScope{SourceRegionId{id(scope[i]).value}}};
      presence[id(i).value]=count>5||mask&(1U<<i)?SourceBindingPresence::kRetained:SourceBindingPresence::kElided;
      if(i)canonical.push_back({id(*parents[i]),SourceRegionId{id(i).value},id(i)});
      if(presence[id(i).value]==SourceBindingPresence::kRetained)
        for(unsigned j=0;j<i;++j)if(presence[id(j).value]==SourceBindingPresence::kRetained&&scope[i]==scope[j])ambiguous=true;
    }
    if(reverse)std::reverse(canonical.begin(),canonical.end());
    auto facts=std::get<SourceNameFacts>(AnalyzeSourceNames(flow,presence,canonical));
    const auto bytes=facts.allocated_bytes;const auto work=facts.work;flow.source_names=std::move(facts.index);
    std::vector<SourceNameQuery> queries;std::vector<SourceNameResolution> expected;
    for(unsigned owner=0;owner<count;++owner) {
      std::optional<unsigned> nearest;
      for(unsigned binding=0;binding<count;++binding)if(presence[id(binding).value]==SourceBindingPresence::kRetained&&contains(scope[binding],owner))
        if(!nearest||contains(scope[*nearest],scope[binding]))nearest=binding;
      for(unsigned binding=0;binding<(count<=5?count:2);++binding) {
        const unsigned target=count<=5?binding:binding?owner:0;
        queries.push_back({id(target),StmtId{id(owner).value}});
        expected.push_back(ambiguous?SourceNameResolution::kAmbiguous:nearest?*nearest==target?SourceNameResolution::kResolved:SourceNameResolution::kShadowed:SourceNameResolution::kUnavailable);
      }
      queries.push_back({NameId{0},StmtId{id(owner).value}});expected.push_back(ambiguous?SourceNameResolution::kAmbiguous:nearest?SourceNameResolution::kShadowed:SourceNameResolution::kResolved);
      queries.push_back({NameId{1},StmtId{id(owner).value}});expected.push_back(SourceNameResolution::kResolved);
    }
    const auto resolved=std::get<SourceNameResults>(ResolveSourceNames(flow,queries));Check(resolved.resolutions==expected);
    if(count>=200||(!mask&&!projected&&!reverse)) {
      Check(std::holds_alternative<SourceNameFacts>(AnalyzeSourceNames(flow,presence,canonical,{bytes,work})));
      Check(std::get<TreeStructureError>(AnalyzeSourceNames(flow,presence,canonical,{bytes-1,work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeSourceNames(flow,presence,canonical,{bytes,work-1}))==TreeStructureError::kWorkLimit);
      Check(std::holds_alternative<SourceNameResults>(ResolveSourceNames(flow,queries,{resolved.allocated_bytes,resolved.work})));
      Check(std::get<TreeStructureError>(ResolveSourceNames(flow,queries,{resolved.allocated_bytes-1,resolved.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(ResolveSourceNames(flow,queries,{resolved.allocated_bytes,resolved.work-1}))==TreeStructureError::kWorkLimit);
    }
    ++cases;return work;
  };
  for(unsigned encoded=0;encoded<24;++encoded)for(unsigned projected=0;projected<8;++projected)
    for(unsigned mask=0;mask<32;++mask)for(bool reverse:{false,true})run(5,encoded,projected,mask,reverse,false);
  for(bool star:{false,true})for(bool reverse:{false,true}) {
    std::uint64_t previous=0;for(unsigned count:{200U,201U,400U,800U}) {
      const auto work=run(count,0,0,0,reverse,star);if(count==800)Check(work<previous*3);previous=work;
    }
  }
  // Enclosing original environments share the whole body envelope, but retain
  // their canonical self/parameter/capture precedence; unrelated layers do not.
  RecoveryFlow flow{};flow.bindings.resize(3);flow.source_spellings.resize(3,SourceSpellingId{0});
  for(auto& binding:flow.bindings)binding.scope=EnclosingBindingScope{};
  flow.source_name_spellings={SourceSpellingId{0},std::nullopt};flow.source_occurrences={SourcePosition{0}};
  std::vector<SourceBindingPresence> presence(3,SourceBindingPresence::kRetained);
  std::vector<SourceBindingShadow> canonical{{{0},{},SymbolId{1}},{{1},{},SymbolId{2}}};
  auto publish=[&](){auto facts=std::get<SourceNameFacts>(AnalyzeSourceNames(flow,presence,canonical));flow.source_names=std::move(facts.index);};
  const std::vector<SourceNameQuery> queries{{SymbolId{0},StmtId{0}},{SymbolId{1},StmtId{0}},{SymbolId{2},StmtId{0}},{NameId{0},StmtId{0}},{NameId{1},{}}};
  publish();auto results=std::get<SourceNameResults>(ResolveSourceNames(flow,queries)).resolutions;
  Check(results==std::vector<SourceNameResolution>{SourceNameResolution::kShadowed,SourceNameResolution::kShadowed,SourceNameResolution::kResolved,SourceNameResolution::kShadowed,SourceNameResolution::kResolved});
  presence[1]=SourceBindingPresence::kElided;publish();Check(std::get<SourceNameResults>(ResolveSourceNames(flow,queries)).resolutions[2]==SourceNameResolution::kResolved);
  canonical.clear();publish();Check(std::get<SourceNameResults>(ResolveSourceNames(flow,queries)).resolutions[2]==SourceNameResolution::kAmbiguous);
  flow.bindings[2].scope=UnmodeledBindingScope{};publish();Check(std::get<SourceNameResults>(ResolveSourceNames(flow,queries)).resolutions[2]==SourceNameResolution::kUnmodeled);
  flow.source_names.reset();Check(std::get<SourceNameResults>(ResolveSourceNames(flow,queries)).resolutions[2]==SourceNameResolution::kUnmodeled);
  flow.bindings[2].scope=OriginalBindingScope{};publish();Check(std::get<SourceNameResults>(ResolveSourceNames(flow,queries)).resolutions[2]==SourceNameResolution::kUnmodeled);
  RecoveryFlow boundary{};boundary.bindings.resize(1);boundary.bindings[0].scope=OriginalBindingScope{SourceRegionId{0}};
  boundary.source_regions={{{0},{1}}};boundary.source_occurrences={SourcePosition{0},SourcePosition{1}};
  boundary.source_spellings={SourceSpellingId{0}};boundary.source_name_spellings={SourceSpellingId{0}};
  const std::vector<SourceBindingPresence> retained{SourceBindingPresence::kRetained};
  auto index=std::get<SourceNameFacts>(AnalyzeSourceNames(boundary,retained,{}));boundary.source_names=std::move(index.index);
  const std::vector<SourceNameQuery> endpoints{{SymbolId{0},StmtId{0}},{SymbolId{0},StmtId{1}},{NameId{0},StmtId{0}},{NameId{0},StmtId{1}},{SymbolId{0},{}}};
  Check(std::get<SourceNameResults>(ResolveSourceNames(boundary,endpoints)).resolutions==std::vector<SourceNameResolution>{
      SourceNameResolution::kResolved,SourceNameResolution::kUnavailable,SourceNameResolution::kShadowed,SourceNameResolution::kResolved,SourceNameResolution::kUnmodeled});
  for(const auto query:{SourceNameQuery{SymbolId{1},StmtId{0}},SourceNameQuery{NameId{1},StmtId{0}},SourceNameQuery{SymbolId{0},StmtId{2}}}) {
    const std::array queries{query};Check(std::get<TreeStructureError>(ResolveSourceNames(boundary,queries))==TreeStructureError::kOwnership);
  }
  boundary.source_spellings[0]={1};Check(std::get<TreeStructureError>(AnalyzeSourceNames(boundary,retained,{}))==TreeStructureError::kOwnership);
  boundary.source_spellings[0]={0};boundary.bindings[0].scope=ProjectedBindingScope{{1}};
  Check(std::get<TreeStructureError>(AnalyzeSourceNames(boundary,retained,{}))==TreeStructureError::kOwnership);
  boundary.bindings.resize(2);boundary.source_spellings.resize(2,SourceSpellingId{0});boundary.source_regions={{{0},{2}},{{1},{3}}};
  boundary.bindings[0].scope=OriginalBindingScope{SourceRegionId{0}};boundary.bindings[1].scope=ProjectedBindingScope{{1}};
  const std::vector<SourceBindingPresence> crossing(2,SourceBindingPresence::kRetained);
  Check(std::get<TreeStructureError>(AnalyzeSourceNames(boundary,crossing,{}))==TreeStructureError::kOwnership);
  RecoveryFlow empty{};empty.source_name_spellings={std::nullopt};auto empty_names=std::get<FlowBindingNames>(AnalyzeFlowBindingNames(empty,{}));empty.source_names=std::move(empty_names.index);
  const std::array free{SourceNameQuery{NameId{0},{}}};Check(std::get<SourceNameResults>(ResolveSourceNames(empty,free)).resolutions[0]==SourceNameResolution::kResolved);
  std::cout<<"independent current source name cases "<<cases<<'\n';
}
void RetirementOracle() {
  std::uint64_t cases=0;
  // Slow independent lexical-ancestry fixed point, test-only. Each source name
  // is resolved from retained bindings rather than production range indexes.
  for(unsigned count=2;count<=6;++count)for(unsigned layout=0;layout<4;++layout)
  for(unsigned mask=0;mask<(1U<<(count-1));++mask)for(unsigned pattern=0;pattern<8;++pattern)for(bool reverse:{false,true}) {
    const auto id=[&](unsigned i){return SymbolId{reverse?count-1-i:i};};
    RecoveryFlow flow{};flow.bindings.resize(count);flow.source_regions.resize(count);flow.source_occurrences.resize(count);
    flow.source_spellings.resize(count);flow.source_name_spellings={SourceSpellingId{0},SourceSpellingId{1}};
    flow.binding_presence.assign(count,SourceBindingPresence::kRetained);
    std::vector<std::optional<unsigned>> parents(count);std::vector<unsigned> spelling(count),scope(count);
    for(unsigned i=1;i<count;++i)parents[i]=layout==0?0:layout==1?i-1:layout==2?(i-1)/2:(i+pattern)%i;
    const auto contains=[&](unsigned outer,unsigned inner){auto at=std::optional{inner};while(at){if(*at==outer)return true;at=parents[*at];}return false;};
    std::size_t cursor=0;
    const auto visit=[&](auto&& self,unsigned node)->void {
      const SourcePosition first{cursor++};flow.source_occurrences[node]=first;
      for(unsigned child=node+1;child<count;++child)if(parents[child]==node)self(self,child);
      flow.source_regions[node]={first,SourcePosition{cursor}};
    };visit(visit,0);
    std::vector<SourceRetirement> candidates(count,SourceRetirement::kFixed);
    std::vector<bool> retained(count,true);
    for(unsigned i=0;i<count;++i){spelling[i]=(pattern>> (i%3))&1;scope[i]=(pattern&4)&&i+1<count&&contains(i,i+1)?i+1:i;flow.source_spellings[id(i).value]={spelling[i]};flow.bindings[id(i).value].scope=scope[i]==i?SourceBindingScope{OriginalBindingScope{SourceRegionId{i}}}:SourceBindingScope{ProjectedBindingScope{SourceRegionId{scope[i]}}};
      if(i&&(mask&(1U<<(i-1)))){candidates[id(i).value]=SourceRetirement::kProposed;retained[i]=false;}}
    for(unsigned i=1;i<count;++i){auto outer=parents[i];while(outer&&spelling[*outer]!=spelling[i])outer=parents[*outer];if(outer)flow.source_shadows.push_back({id(*outer),SourceRegionId{i},id(i)});}
    auto original=std::get<SourceNameFacts>(AnalyzeSourceNames(flow,flow.binding_presence,flow.source_shadows));flow.source_names=std::move(original.index);
    std::vector<SourceRetirementQuery> queries;std::vector<SourceRetentionDemand> demands;
    for(unsigned i=1;i<count;++i)if(candidates[id(i).value]==SourceRetirement::kProposed) {
      const auto condition=pattern&1?std::optional{id(i==count-1?1:i+1)}:std::nullopt;
      for(unsigned owner=0;owner<count;++owner)for(unsigned root=0;root<count;++root)if(candidates[id(root).value]==SourceRetirement::kFixed)queries.push_back({{pattern&2?std::variant<SymbolId,NameId>{NameId{spelling[root]}}:std::variant<SymbolId,NameId>{id(root)},StmtId{owner}},id(i),condition});
      if(pattern&4)demands.push_back({id(i),i==1?std::nullopt:std::optional{id(i-1)}});
    }
    const auto active=[&](std::optional<SymbolId> condition){return !condition||retained[reverse?count-1-condition->value:condition->value];};
    bool changed=true;
    while(changed){changed=false;
      const auto keep=[&](SymbolId binding){const auto i=reverse?count-1-binding.value:binding.value;if(!retained[i]){retained[i]=true;changed=true;}};
      for(const auto& demand:demands)if(active(demand.retained_by))keep(demand.binding);
      for(const auto& query:queries)if(active(query.retained_by)) {
        const unsigned owner=query.lookup.owner->value;
        const auto* symbol=std::get_if<SymbolId>(&query.lookup.name);const auto root=symbol?std::optional<unsigned>{reverse?count-1-symbol->value:symbol->value}:std::nullopt;
        const auto name=root?spelling[*root]:std::get<NameId>(query.lookup.name).value;
        std::optional<unsigned> selected;bool ambiguous=false;
        for(unsigned binding=0;binding<count;++binding)if(retained[binding]&&spelling[binding]==name) {
          for(unsigned other=0;other<binding;++other)if(retained[other]&&spelling[other]==name&&scope[other]==scope[binding])ambiguous=true;
          if(contains(scope[binding],owner)&&(!selected||contains(scope[*selected],scope[binding])))selected=binding;
        }
        const bool valid=!ambiguous&&(root?selected==root:!selected);
        if(!valid)keep(query.binding);
      }
    }
    auto analyzed=AnalyzeSourceNameRetirement(flow,candidates,queries,demands);Check(std::holds_alternative<SourceNameRetirement>(analyzed));const auto result=std::get<SourceNameRetirement>(std::move(analyzed));
    for(unsigned i=0;i<count;++i)Check((result.presence[id(i).value]==SourceBindingPresence::kRetained)==retained[i]);++cases;
    if(count==4&&layout==1&&mask==7&&pattern==4&&!reverse) {
      const auto exact=analysis::SemanticBudget{result.allocated_bytes,result.work};
      Check(std::holds_alternative<SourceNameRetirement>(AnalyzeSourceNameRetirement(flow,candidates,queries,demands,exact)));
      Check(std::get<TreeStructureError>(AnalyzeSourceNameRetirement(flow,candidates,queries,demands,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeSourceNameRetirement(flow,candidates,queries,demands,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
      auto invalid=candidates;invalid[0]=SourceRetirement::kProposed;Check(std::get<TreeStructureError>(AnalyzeSourceNameRetirement(flow,invalid,queries,demands))==TreeStructureError::kOwnership);
      auto bad=queries;bad[0].retained_by=SymbolId{count};Check(std::get<TreeStructureError>(AnalyzeSourceNameRetirement(flow,candidates,bad,demands))==TreeStructureError::kOwnership);
    }
  }
  // A name-activation chain and conditional-copy chain share one bounded queue.
  // Each query is invalidated once even with nested overlap; fixed descendants
  // partition unaffected queries away from ancestor activations.
  std::uint64_t previous=0;
  for(unsigned count:{200U,201U,400U,800U,1600U}) {
    RecoveryFlow flow{};flow.bindings.resize(count+1);flow.source_spellings.resize(count+1,SourceSpellingId{0});
    flow.binding_presence.resize(count+1,SourceBindingPresence::kRetained);flow.source_regions.resize(count+1);flow.source_occurrences.resize(count);
    std::vector<SourceRetirement> candidates(count+1,SourceRetirement::kProposed);candidates[0]=SourceRetirement::kFixed;
    for(unsigned i=0;i<=count;++i){flow.bindings[i].scope=OriginalBindingScope{SourceRegionId{i}};flow.source_regions[i]={{i},{2*count+1-i}};if(i)flow.source_shadows.push_back({{i-1},SourceRegionId{i},SymbolId{i}});}
    std::vector<SourceRetirementQuery> queries;
    for(unsigned i=1;i<=count;++i){flow.source_occurrences[i-1]=SourcePosition{i};queries.push_back({{SymbolId{0},StmtId{i-1}},SymbolId{i==count?count:i+1},{}});}
    auto original=std::get<SourceNameFacts>(AnalyzeSourceNames(flow,flow.binding_presence,flow.source_shadows));flow.source_names=std::move(original.index);
    const std::array demands{SourceRetentionDemand{SymbolId{1},{}}};
    auto result=std::get<SourceNameRetirement>(AnalyzeSourceNameRetirement(flow,candidates,queries,demands));
    for(const auto presence:result.presence)Check(presence==SourceBindingPresence::kRetained);
    if(count>=400)Check(result.work<previous*3);previous=result.work;
    std::cout<<"name retirement size "<<count<<" work "<<result.work<<'\n';
  }
  for(unsigned variant=0;variant<3;++variant) {
    RecoveryFlow flow{};flow.bindings.resize(4);flow.binding_presence.assign(4,SourceBindingPresence::kRetained);
    flow.source_spellings={{0},{0},{1},{1}};flow.source_name_spellings={SourceSpellingId{0},std::nullopt};
    flow.source_regions={{{0},{10}},{{1},{9}},{{2},{8}},{{3},{7}}};flow.source_occurrences={SourcePosition{5}};
    for(unsigned i=0;i<4;++i)flow.bindings[i].scope=OriginalBindingScope{SourceRegionId{i}};
    if(variant==1)flow.bindings[1].scope=UnmodeledBindingScope{};
    if(variant==2)flow.bindings[1].scope=ProjectedBindingScope{{0}};
    flow.source_shadows={{{0},SourceRegionId{1},SymbolId{1}},{{2},SourceRegionId{3},SymbolId{3}}};
    auto original=std::get<SourceNameFacts>(AnalyzeSourceNames(flow,flow.binding_presence,flow.source_shadows));flow.source_names=std::move(original.index);
    const std::array candidates{SourceRetirement::kFixed,SourceRetirement::kProposed,SourceRetirement::kProposed,SourceRetirement::kProposed};
    // Query 3 fails while its owner 2 is absent, installing a latent demand.
    const std::array queries{SourceRetirementQuery{{SymbolId{0},StmtId{0}},SymbolId{3},SymbolId{2}},SourceRetirementQuery{{SymbolId{0},StmtId{0}},SymbolId{2},{}}};
    const std::array seeds{SourceRetentionDemand{SymbolId{1},{}}};
    auto result=std::get<SourceNameRetirement>(AnalyzeSourceNameRetirement(flow,candidates,queries,seeds));
    for(const auto presence:result.presence)Check(presence==SourceBindingPresence::kRetained);
    auto inert=std::get<SourceNameRetirement>(AnalyzeSourceNameRetirement(flow,candidates,queries,{}));
    Check(inert.presence[1]==SourceBindingPresence::kElided&&inert.presence[2]==SourceBindingPresence::kElided&&inert.presence[3]==SourceBindingPresence::kElided);
    flow.source_names.reset();auto gap=std::get<SourceNameRetirement>(AnalyzeSourceNameRetirement(flow,candidates,queries,{}));for(const auto presence:gap.presence)Check(presence==SourceBindingPresence::kRetained);
  }
  std::cout<<"independent name retirement cases "<<cases<<'\n';
}
void SpellingOracle() {
  const auto run=[](unsigned count,unsigned encoded,unsigned pattern,bool reverse) {
    const auto index=[&](unsigned i){return reverse?count-1-i:i;};
    FunctionTree tree{};tree.parameter_scope={count};tree.body_scope={index(0)};
    tree.scopes.resize(count+1);tree.scopes[count]={ScopeKind::kParameters,{}};
    tree.names={u"left",u"right",u"left"};tree.symbols.resize(count);
    std::vector<std::optional<unsigned>> parents(count);
    for(unsigned i=0;i<count;++i) {
      if(i){parents[i]=encoded%i;encoded/=i;}
      tree.scopes[index(i)]={i?ScopeKind::kBlock:ScopeKind::kFunctionBody,ScopeId{i?index(*parents[i]):count}};
      tree.symbols[index(i)]={{index(i)},{pattern%3},BindingKind::kCompilerTemporary,DeclarationKind::kLet,
          NameEvidence::kSynthesized,TemporaryReason::kMultipleUses};pattern/=3;
    }
    TreeCheckBudget budget({});const auto facts=AnalyzeTreeSpellings(tree,budget);
    const auto bad_presence=[&](std::vector<SourceBindingPresence> presence) {
      bool rejected=false;try{TreeCheckBudget checked({});TreeScopeCheck checker(tree,checked,presence);}
      catch(TreeStructureError error){rejected=error==TreeStructureError::kOwnership;}Check(rejected);
    };
    bad_presence(std::vector<SourceBindingPresence>(count+1,SourceBindingPresence::kRetained));
    bad_presence(std::vector<SourceBindingPresence>(count,static_cast<SourceBindingPresence>(255)));
    for(unsigned i=0;i<count;++i) {
      const auto& name=tree.names[tree.symbols[index(i)].name.value];
      std::optional<SymbolId> expected;auto parent=parents[i];
      while(parent){if(tree.names[tree.symbols[index(*parent)].name.value]==name){expected=SymbolId{index(*parent)};break;}parent=parents[*parent];}
      unsigned matches=0;for(const auto& symbol:tree.symbols)matches+=tree.names[symbol.name.value]==name;
      Check(facts.shadowed[index(i)]==expected);Check(bool(facts.unique[index(i)])==(matches==1));
      Check(facts.names[tree.symbols[index(i)].name.value]==facts.bindings[index(i)]);
    }
    // Every retained-name mask is checked independently of production's forest.
    if(count==5)for(unsigned mask=0;mask<32;++mask) {
      std::vector<SourceBindingPresence> presence(count);
      for(unsigned i=0;i<count;++i)presence[index(i)]=mask&(1U<<i)?SourceBindingPresence::kRetained:SourceBindingPresence::kElided;
      std::vector<TreeScopedReference> queries;std::vector<TreeScopedName> names;
      std::vector<bool> expected,free;
      for(unsigned scope=0;scope<count;++scope)for(unsigned binding=0;binding<count;++binding) {
        const auto name=tree.symbols[index(binding)].name;
        auto owner=std::optional{scope};std::optional<SymbolId> nearest;
        while(owner){if((mask&(1U<<*owner))&&tree.names[tree.symbols[index(*owner)].name.value]==tree.names[name.value]){nearest=SymbolId{index(*owner)};break;}owner=parents[*owner];}
        queries.push_back({SymbolId{index(binding)},ScopeId{index(scope)}});names.push_back({name,ScopeId{index(scope)}});
        expected.push_back(nearest==SymbolId{index(binding)});free.push_back(!nearest);
      }
      TreeCheckBudget current({});const auto resolved=ResolveTreeReferences(tree,queries,current,presence);
      const auto resolved_names=ResolveTreeNames(tree,names,current,presence);
      for(unsigned i=0;i<queries.size();++i){Check(bool(resolved[i])==expected[i]);Check(bool(resolved_names[i])==free[i]);}
    }
  };
  for(unsigned encoded=0;encoded<24;++encoded)for(unsigned pattern=0;pattern<27;++pattern)
    for(bool reverse:{false,true})run(5,encoded,pattern,reverse);
  for(unsigned count:{200U,201U,400U,800U})for(bool reverse:{false,true})run(count,1,1,reverse);
}
void StorageNameOracle() {
  std::uint64_t cases=0;
  for(unsigned count=1;count<=4;++count)for(unsigned encoded=0;encoded<(1U<<(count*2));++encoded)
    for(unsigned positions=0;positions<(1U<<count);++positions)for(bool reverse:{false,true}) {
      const auto index=[&](unsigned i){return reverse?count-1-i:i;};
      BindingInterference graph;std::vector<StorageBinding> bindings(count);
      std::vector<StorageShadow> shadows;std::vector<StorageReference> reads;
      unsigned word=encoded;
      for(unsigned i=0;i<count;++i) {
        const SymbolId id{index(i)};bindings[id.value].eligible=true;
        bindings[id.value].unique_spelling=false;bindings[id.value].native_declaration=i%2==0;
        bindings[id.value].initialization_order=i+1;
        reads.push_back({id,SourcePosition{positions&(1U<<i)?2U:0U}});
        switch(word%4) {
          case 0:break;
          case 1:shadows.push_back({id,SourceRegion{{0},{1}}});break;
          case 2:shadows.push_back({id,SourceRegion{{2},{3}}});break;
          case 3:shadows.push_back({id,{}});break;
        }
        word/=4;if(i)graph.affinities.push_back({SymbolId{index(0)},id,false});
      }
      const auto rank=[&](unsigned candidate) {
        bool visible=true;
        for(const auto& shadow:shadows)if(shadow.binding.value==candidate) {
          if(!shadow.region)visible=false;
          else for(const auto& read:reads)if(read.position>=shadow.region->first&&read.position<shadow.region->last)visible=false;
        }
        return std::tuple{!visible,!bindings[candidate].native_declaration,*bindings[candidate].initialization_order,candidate};
      };
      unsigned expected=0;for(unsigned i=1;i<count;++i)if(rank(i)<rank(expected))expected=i;
      const auto result=std::get<StorageCoalescing>(PlanStorageCoalescing(graph,bindings,{},StorageNames{shadows,reads}));
      for(unsigned i=0;i<count;++i)Check(result.storage[i]==(i==expected?std::nullopt:std::optional{SymbolId{expected}}));
      if(count==4&&!encoded&&!positions&&!reverse) {
        Check(std::holds_alternative<StorageCoalescing>(PlanStorageCoalescing(graph,bindings,{result.allocated_bytes,result.work},{shadows,reads})));
        Check(std::get<TreeStructureError>(PlanStorageCoalescing(graph,bindings,{result.allocated_bytes-1,result.work},{shadows,reads}))==TreeStructureError::kResourceLimit);
        Check(std::get<TreeStructureError>(PlanStorageCoalescing(graph,bindings,{result.allocated_bytes,result.work-1},{shadows,reads}))==TreeStructureError::kWorkLimit);
      }
      ++cases;
    }
  std::cout<<"independent storage name cases "<<cases<<'\n';
}
void StoragePrerequisitePreference() {
  unsigned cases=0;
  for(unsigned availability=0;availability<16;++availability)for(unsigned names=0;names<4;++names)for(unsigned style=0;style<4;++style)for(bool reverse:{false,true}) {
    const auto id=[&](unsigned i){return SymbolId{reverse?1-i:i};};
    BindingInterference facts;facts.affinities.push_back({id(0),id(1),false});
    std::array<StorageBinding,2> bindings;std::vector<StorageShadow> shadows;
    const std::array reads{StorageReference{id(0),{2}},StorageReference{id(1),{2}}};
    for(unsigned i=0;i<2;++i) {
      auto& binding=bindings[id(i).value];binding.eligible=true;binding.reference_region=SourceRegion{{2},{3}};
      binding.binding_region=availability&(1U<<i)?SourceRegion{{0},{4}}:SourceRegion{{0},{1}};
      binding.reachable_references=CompletionRegion{2,3};binding.initialized_region=availability&(1U<<(i+2))?CompletionRegion{0,4}:CompletionRegion{0,1};
      binding.unique_spelling=(style&(1U<<i))!=0;binding.native_declaration=i==0;binding.initialization_order=i+1;
      if(names&(1U<<i))shadows.push_back({id(i),SourceRegion{{2},{3}}});
    }
    const auto rank=[&](unsigned i){const bool feasible=(availability&(1U<<i))&&(availability&(1U<<(i+2)));return std::tuple{!feasible,(names&(1U<<i))!=0,!(style&(1U<<i)),i!=0,i,id(i).value};};
    const auto winner=rank(0)<rank(1)?id(0):id(1);
    const auto result=std::get<StorageCoalescing>(PlanStorageCoalescing(facts,bindings,{},StorageNames{shadows,reads}));
    for(unsigned i=0;i<2;++i)Check(result.storage[i]==(i==winner.value?std::nullopt:std::optional{winner}));++cases;
  }
  std::cout<<"independent storage prerequisite ranks "<<cases<<'\n';
}
void StorageInitializationSubsets() {
  unsigned cases=0;
  for(unsigned edges=0;edges<4;++edges)for(unsigned roots=0;roots<4;++roots)for(unsigned mask=0;mask<4;++mask) {
    RecoveryFlow flow{};flow.entry={0};flow.events.resize(7);flow.statements.resize(4);flow.bindings.resize(4);
    for(unsigned i=0;i<6;++i)Link(flow,i,i+1);
    if(edges&1){Link(flow,0,2);flow.edges.back().completion=FlowCompletion::kThrow;}
    if(edges&2){Link(flow,5,2);flow.edges.back().completion=FlowCompletion::kResumeNext;}
    for(unsigned i=0;i<4;++i){flow.bindings[i]={BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate,StmtId{i}};flow.definitions.push_back({{i+1},{i},FlowDefinitionKind::kUndefined,{},{},StmtId{i}});flow.events[i+1].definition=FlowDefinitionId{i};}
    flow.events[5].read=SymbolId{2};flow.events[6].read=SymbolId{3};
    const auto reachable=[&](unsigned removed,unsigned destination){std::array<bool,7> seen{};std::vector<unsigned> pending{0};seen[0]=true;while(!pending.empty()){const auto from=pending.back();pending.pop_back();for(const auto& edge:flow.edges)if(edge.from.value==from&&edge.to.value!=removed&&!seen[edge.to.value]){seen[edge.to.value]=true;pending.push_back(edge.to.value);}}return seen[destination];};
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));const auto initialized=std::get<FlowInitializations>(AnalyzeFlowInitializations(flow));
    std::array<std::optional<SymbolId>,4> proposal{};proposal[2]=SymbolId{roots&1};proposal[3]=SymbolId{(roots>>1)&1};
    const auto full=std::get<StorageInitializations>(PlanStorageInitializations(flow,live,initialized,proposal));
    for(unsigned i=0;i<2;++i)if(!(mask&(1U<<i)))proposal[i+2].reset();
    const auto subset=std::get<StorageInitializations>(PlanStorageInitializations(flow,live,initialized,proposal));
    for(unsigned i=2;i<4;++i) {
      const bool blocked=proposal[i]&&reachable(proposal[i]->value+1,i+1);
      Check(bool(subset.blocked[i])==blocked);Check(subset.stores[i]==(proposal[i]&&!blocked?proposal[i]:std::nullopt));
      if(proposal[i])Check(subset.stores[i]==full.stores[i]&&subset.blocked[i]==full.blocked[i]);
    }
    ++cases;
  }
  std::cout<<"independent storage initialization subsets "<<cases<<'\n';
}
void StorageNameHoles() {
  for(bool observed:{false,true})for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{2U,200U,201U,400U,800U}) {
      const auto id=[&](unsigned i){return SymbolId{reverse?count-1-i:i};};
      BindingInterference graph;std::vector<StorageBinding> bindings(count);
      std::vector<StorageShadow> shadows;std::vector<StorageReference> reads;
      for(unsigned i=0;i<count;++i) {
        bindings[id(i).value].eligible=true;bindings[id(i).value].unique_spelling=i!=0;
        bindings[id(i).value].initialization_order=i+1;
        // Group uses are at 0 and 2: the hole [1,2) lies inside the bounding
        // extent but contains no use. A min/max visibility approximation fails.
        const auto hidden=i==0?(observed?SourceRegion{{2},{3}}:SourceRegion{{1},{2}}):
            i==1?(observed?SourceRegion{{3},{4}}:SourceRegion{{2},{3}}):SourceRegion{{0},{1}};
        shadows.push_back({id(i),hidden});
        reads.push_back({id(i),{i+1==count?2U:0U}});
        if(i)graph.affinities.push_back({id(0),id(i),false});
      }
      if(reverse){std::reverse(graph.affinities.begin(),graph.affinities.end());std::reverse(reads.begin(),reads.end());std::reverse(shadows.begin(),shadows.end());}
      const auto plan=std::get<StorageCoalescing>(PlanStorageCoalescing(graph,bindings,{},StorageNames{shadows,reads}));
      const auto expected=id(observed?1:0);
      for(unsigned i=0;i<count;++i)Check(plan.storage[i]==(SymbolId{i}==expected?std::nullopt:std::optional{expected}));
      if(count==800)Check(plan.work<previous*3);previous=plan.work;
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(graph,bindings,{plan.allocated_bytes-1,plan.work},{shadows,reads}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(graph,bindings,{plan.allocated_bytes,plan.work-1},{shadows,reads}))==TreeStructureError::kWorkLimit);
    }
  }
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
  for(std::size_t i=0;i<owners.size();++i)Check(flow.expression_owners[i]==owners[i]);
  // Supplied normal values are authoritative even when unknown. The coarse
  // direct-read fallback must not infer a second answer for the same projection.
  for(bool known:{false,true}){
    auto projected=projection;std::vector<std::optional<SymbolId>> results(tree.expressions.size());
    if(known)results={SymbolId{0},SymbolId{1},SymbolId{1},SymbolId{2},SymbolId{2},SymbolId{2}};
    projected.normal_bindings=results;const auto normal=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projected));
    for(const auto& definition:normal.definitions){
      if(known&&definition.kind==FlowDefinitionKind::kAssignment)Check(definition.copy==SymbolId{definition.binding.value==1?0U:1U});
      else Check(!definition.copy);}
    Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projected,{normal.allocated_bytes,normal.work})));
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projected,{normal.allocated_bytes-1,normal.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projected,{normal.allocated_bytes,normal.work-1}))==TreeStructureError::kWorkLimit);
    results[0]=SymbolId{static_cast<std::uint32_t>(tree.symbols.size())};Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projected))==TreeStructureError::kBinding);
    projected.normal_bindings=std::span<const std::optional<SymbolId>>{results}.first(results.size()-1);
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projected))==TreeStructureError::kOwnership);
  }
  // ReadValues follows the actual slot; assignment/update still use that
  // slot, including both update halves. Old immutable facts stay unchanged.
  for(bool alias:{false,true})for(bool shared:{false,true})for(bool value:{false,true})for(bool occurrence:{false,true}){
    auto projected=projection;auto copy_aliases=aliases;
    std::vector<std::optional<SymbolId>> storage(tree.symbols.size()),read_values(tree.symbols.size());
    if(alias)copy_aliases[2]=SymbolId{1};if(shared)storage[2]=SymbolId{1};if(value)read_values[1]=SymbolId{0};
    std::vector<std::optional<SymbolId>> sources(tree.expressions.size());
    if(occurrence){sources[5]=SymbolId{0};sources[4]=SymbolId{0};}
    projected.aliases=copy_aliases;projected.storage=storage;projected.read_bindings=read_values;projected.read_sources=sources;
    const auto composed=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projected));
    unsigned writes=0,update_reads=0,returns=0;
    for(const auto& reference:composed.references){const auto expression=std::get<ExprId>(reference.site);
      if(expression==ExprId{3}&&reference.kind==FlowReferenceKind::kWrite){Check(reference.binding==SymbolId{alias||shared?1U:2U});++writes;}
      if(expression==ExprId{4}){Check(reference.binding==SymbolId{alias||shared?1U:2U});if(reference.kind==FlowReferenceKind::kRead)++update_reads;else ++writes;}
      if(expression==ExprId{5}){Check(reference.binding==SymbolId{occurrence?0U:alias||shared?(value?0U:1U):2U});Check(reference.owner==StmtId{6});++returns;}}
    Check(writes==2&&update_reads==1&&returns==1);
    auto invalid=projected;invalid.read_sources=std::span<const std::optional<SymbolId>>{sources}.first(sources.size()-1);
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,invalid))==TreeStructureError::kOwnership);
    auto out_of_range=sources;out_of_range[5]=SymbolId{static_cast<std::uint32_t>(tree.symbols.size())};invalid.read_sources=out_of_range;
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,invalid))==TreeStructureError::kBinding);
    Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projected,{composed.allocated_bytes,composed.work})));
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projected,{composed.allocated_bytes-1,composed.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projected,{composed.allocated_bytes,composed.work-1}))==TreeStructureError::kWorkLimit);
  }
  // The same original read now executes in the return, not in its omitted
  // source store. Rebuilding must publish the destination, without mutating
  // the previous immutable result or confusing the read with its replacement.
  omitted[3]=1;replacements[5]=ExprId{0};
  const auto moved=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  unsigned forwarded=0;
  for(const auto& reference:moved.references)if(reference.site==FlowReferenceSite{ExprId{0}}) {
    Check(reference.owner==StmtId{6});++forwarded;
  }
  Check(forwarded==1);Check(moved.expression_owners[0]==StmtId{6});Check(!moved.expression_owners[1]);
  omitted[3]=0;
  std::vector<std::optional<ExpressionTransport>> transports(tree.expressions.size());
  std::vector<std::optional<SymbolId>> transported_reads(tree.expressions.size());
  transports[1]=ExpressionTransport{{0},{0}};
  transported_reads[5]=SymbolId{1};
  auto transport_projection=projection;transport_projection.transports=transports;transport_projection.transported_reads=transported_reads;
  const auto transported=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,transport_projection));
  Check(transported.expression_owners[1]==StmtId{3});Check(transported.expression_owners[0]==StmtId{6});
  Check(!transported.expression_transports[1]);Check(transported.expression_transports[0]==SymbolId{1});
  forwarded=0;
  for(const auto& reference:transported.references) {
    Check(reference.site!=FlowReferenceSite{ExprId{1}});
    if(reference.site==FlowReferenceSite{ExprId{0}}){Check(reference.owner==StmtId{6});++forwarded;}
  }
  Check(forwarded==1);
  // Nested RHSs inherit the outer transport identity. This is the dependency
  // on its projected environment, not a grouping by destination statement.
  transports[3]=ExpressionTransport{{2},{0}};replacements[5]=ExprId{2};replacements[2]=ExprId{0};
  transported_reads[5]=SymbolId{2};transported_reads[2]=SymbolId{1};
  auto nested=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,transport_projection));
  Check(nested.expression_transports[0]==SymbolId{2});Check(nested.expression_transports[2]==SymbolId{2});
  Check(nested.expression_owners[0]==StmtId{6});Check(nested.expression_owners[2]==StmtId{6});
  Check(!nested.expression_transports[1]);Check(!nested.expression_transports[3]);
  auto committed_transport=transport_projection;committed_transport.transported_reads={};
  const auto reset=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,committed_transport,{},std::move(nested)));
  Check(reset.expression_transports.empty());
  replacements[5].reset();replacements[2].reset();
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
  Check(cleared.names.empty());Check(cleared.expression_owners[0]==StmtId{3});

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
  // Original bindings use the same published facts and boundary validation.
  source_bindings[1]=OriginalBindingScope{SourceRegionId{1}};
  const auto original=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}}));
  Check(original.BindingInScope({1},StmtId{3})&&!original.BindingInScope({1},StmtId{4}));
  Check(!original.BindingInScope({1},{}));
  source_bindings[1]=OriginalBindingScope{SourceRegionId{2}};
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}}))==TreeStructureError::kOwnership);
  source_bindings[1]=ProjectedBindingScope{{2}};
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings}}))==TreeStructureError::kOwnership);
  source_bindings[1]=OriginalBindingScope{SourceRegionId{1}};
  std::vector<SourceBindingShadow> shadows{{{1},SourceRegionId{1}}};
  std::vector<std::uint8_t> unique(tree.symbols.size(),1);unique[1]=0;
  auto spelled=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings,shadows,unique}}));
  Check(spelled.source_shadows.size()==1&&!spelled.bindings[1].unique_spelling);
  shadows[0].region=SourceRegionId{2};
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings,shadows,unique}}))==TreeStructureError::kOwnership);
  shadows[0]={SymbolId{static_cast<std::uint32_t>(tree.symbols.size())},{}};
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings,{},{},{},{},{regions,positions,source_bindings,shadows,unique}}))==TreeStructureError::kOwnership);
  std::fill(spelled.binding_presence.begin(),spelled.binding_presence.end(),SourceBindingPresence::kElided);
  spelled.source_names=SourceNameIndex{};spelled.source_spellings.assign(tree.symbols.size(),SourceSpellingId{0});spelled.source_name_spellings={SourceSpellingId{0}};
  const auto unnamed=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,
      {aliases,literals,replacements,omitted,evaluations,normal,native,private_bindings},{},std::move(spelled)));
  Check(unnamed.source_shadows.empty()&&unnamed.bindings[1].unique_spelling);
  Check(!unnamed.source_names&&unnamed.source_spellings.empty()&&unnamed.source_name_spellings.empty());
  Check(unnamed.binding_presence.size()==tree.symbols.size()&&unnamed.binding_presence[1]==SourceBindingPresence::kRetained);
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
  unsigned cases=0;
  for(unsigned encoded=0;encoded<variants;++encoded)for(unsigned policy=0;policy<11;++policy){
    BindingInterference facts;std::array<std::array<bool,bindings>,bindings> conflict{};
    unsigned remaining=encoded;
    for(unsigned a=0;a<bindings;++a)for(unsigned b=a+1;b<bindings;++b){
      const auto kind=remaining%4;remaining/=4;
      if(kind&1)facts.affinities.push_back({{a},{b},bool(kind&2)});
      if(kind&2){facts.conflicts.push_back({{a},{b}});conflict[a][b]=conflict[b][a]=true;}
    }
    std::array<StorageBinding,bindings> admission{};std::array<unsigned,bindings> owner{};
    for(unsigned i=0;i<bindings;++i){owner[i]=i;admission[i]={policy!=1||i!=0,policy!=2||i%2==0,policy==3?bindings-i:i,policy!=4||i%2==0};
      if(policy>=5) {
        admission[i].reference_region=i%2?SourceRegion{{6},{7}}:SourceRegion{{2},{3}};
        switch((i+policy)%4) {
          case 0:break;
          case 1:admission[i].binding_region=SourceRegion{{0},{10}};break;
          case 2:admission[i].binding_region=SourceRegion{{2},{4}};break;
          case 3:admission[i].binding_region=SourceRegion{{6},{8}};break;
        }
        admission[i].native_declaration=i%2==0;admission[i].unique_spelling=i%3!=0;
        if(policy==6&&i%2)admission[i].initialization_order.reset();
        admission[i].unlocated_reference=policy==7&&i==3;
      }
      if(policy>=8) {
        admission[i].reachable_references=CompletionRegion{i*2+1,i*2+2};
        admission[i].initialized_region=CompletionRegion{i*2+1,bindings*2+1};
        if(policy==9&&i==0)admission[i].initialized_region.reset();
        if(policy==10&&i%2)admission[i].reachable_references.reset();
      }
    }
    // Independent tiny dense oracle: inspect every cross-group member pair,
    // then rewrite all labels. No DSU, sparse adjacency or production helpers.
    for(const auto& affinity:facts.affinities){const auto a=affinity.first.value,b=affinity.second.value;
      if(!admission[a].eligible||!admission[b].eligible||owner[a]==owner[b])continue;
      const auto first=owner[a],second=owner[b];bool blocked=false;
      for(unsigned i=0;i<bindings;++i)for(unsigned j=0;j<bindings;++j)if(owner[i]==first&&owner[j]==second)blocked|=conflict[i][j];
      if(blocked)continue;
      for(auto& label:owner)if(label==second)label=first;
    }
    // Enumerate every candidate and every original member's observations. This
    // intentionally slow tiny oracle uses neither DSU nor interval summaries.
    const auto rank=[&](unsigned candidate) {
      const auto& slot=admission[candidate];bool covers=true;
      if(slot.binding_region)for(unsigned member=0;member<bindings;++member)if(owner[member]==owner[candidate]) {
        if(admission[member].unlocated_reference)covers=false;
        if(const auto& observed=admission[member].reference_region;observed&&
            (observed->first<slot.binding_region->first||observed->last>slot.binding_region->last))covers=false;
      }
      for(unsigned member=0;member<bindings;++member)if(owner[member]==owner[candidate])
        if(const auto& observed=admission[member].reachable_references;observed&&
            (!slot.initialized_region||observed->first<slot.initialized_region->first||observed->last>slot.initialized_region->last))covers=false;
      return std::tuple{!covers,!slot.unique_spelling,!slot.native_declaration,
          !slot.initialization_order.has_value(),slot.initialization_order.value_or(0),candidate};
    };
    const auto value=PlanStorageCoalescing(facts,admission);Check(std::holds_alternative<StorageCoalescing>(value));
    const auto& result=std::get<StorageCoalescing>(value);
    for(unsigned i=0;i<bindings;++i){auto expected=i;
      for(unsigned candidate=0;candidate<bindings;++candidate)if(owner[candidate]==owner[i]&&rank(candidate)<rank(expected))expected=candidate;
      Check(result.storage[i]==(expected==i?std::nullopt:std::optional{SymbolId{expected}}));}
    if(encoded==variants-1&&policy==0){
      Check(std::holds_alternative<StorageCoalescing>(PlanStorageCoalescing(facts,admission,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,admission,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,admission,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    }
    ++cases;
  }
  std::cout<<"independent whole-group representatives "<<cases<<'\n';
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
void StableValues() {
  // Independent backwards origin closure: two opaque entry values, three
  // copy nodes, arbitrary cycles/conflicts/unknown leaves and reversed IDs.
  for(unsigned mask=0;mask<512;++mask)for(unsigned encoded=0;encoded<64;++encoded) {
    RecoveryFlow flow{};flow.bindings.resize(5);FlowLiveness live{};std::vector<FlowDefinitionValue> values;
    const auto id=[&](unsigned i){return SymbolId{mask%2?4-i:i};};
    const auto add=[&](unsigned target,FlowDefinitionKind kind,FlowDefinitionValue value){flow.definitions.push_back({{},id(target),kind});values.push_back(value);live.live_definitions.push_back(1);};
    for(unsigned i=0;i<5;++i){auto& binding=flow.bindings[id(i).value];binding.kind=i<2?BindingKind::kParameter:BindingKind::kCompilerTemporary;binding.exposure=i<2?FlowBindingExposure::kObservable:FlowBindingExposure::kPrivate;binding.stable_entry=i<2;}
    add(0,FlowDefinitionKind::kParameter,UnknownFlowValue{});add(1,FlowDefinitionKind::kParameter,UnknownFlowValue{});
    std::array<unsigned,3> seeds{};auto code=encoded;
    for(unsigned i=0;i<3;++i){seeds[i]=code%4;code/=4;if(seeds[i])add(i+2,FlowDefinitionKind::kAssignment,seeds[i]==3?FlowDefinitionValue{UnknownFlowValue{}}:FlowDefinitionValue{FlowBindingValue{id(seeds[i]-1)}});}
    for(unsigned source=0;source<3;++source)for(unsigned target=0;target<3;++target)if(mask&(1U<<(source*3+target)))add(target+2,FlowDefinitionKind::kAssignment,FlowBindingValue{id(source+2)});
    const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));
    std::array<bool,3> grounded{};
    for(unsigned root=0;root<3;++root){std::array<bool,3> seen{};std::vector<unsigned> pending{root};seen[root]=true;
      while(!pending.empty()){const auto at=pending.back();pending.pop_back();grounded[root]|=seeds[at]==1||seeds[at]==2;for(unsigned source=0;source<3;++source)if((mask&(1U<<(source*3+at)))&&!seen[source]){seen[source]=true;pending.push_back(source);}}}
    for(unsigned root=0;root<3;++root){std::array<bool,3> seen{};std::vector<unsigned> pending{root};seen[root]=true;bool unknown=false;std::optional<SymbolId> expected;
      while(!pending.empty()){const auto at=pending.back();pending.pop_back();if(seeds[at]==3||!grounded[at])unknown=true;
        else if(seeds[at]){const auto source=id(seeds[at]-1);if(expected&&*expected!=source)unknown=true;else expected=source;}
        for(unsigned source=0;source<3;++source)if((mask&(1U<<(source*3+at)))&&!seen[source]){seen[source]=true;pending.push_back(source);}}
      Check(result.bindings[id(root+2).value]==(unknown?std::nullopt:expected));Check(!result.values[id(root+2).value]);}
  }
  std::uint64_t previous=0;
  for(unsigned count:{200U,201U,400U,800U}){
    RecoveryFlow flow{};flow.bindings.resize(count);FlowLiveness live{};std::vector<FlowDefinitionValue> values;
    for(unsigned i=0;i<count;++i){flow.bindings[i].kind=i?BindingKind::kCompilerTemporary:BindingKind::kParameter;flow.bindings[i].exposure=i?FlowBindingExposure::kPrivate:FlowBindingExposure::kObservable;flow.bindings[i].stable_entry=!i;
      flow.definitions.push_back({{},SymbolId{i},i?FlowDefinitionKind::kAssignment:FlowDefinitionKind::kParameter});values.push_back(i?FlowDefinitionValue{FlowBindingValue{{i-1}}}:FlowDefinitionValue{UnknownFlowValue{}});live.live_definitions.push_back(1);}
    flow.definitions.push_back({{},SymbolId{1},FlowDefinitionKind::kAssignment});values.push_back(FlowBindingValue{{count-1}});live.live_definitions.push_back(1);
    const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));for(const auto& value:result.bindings)Check(value==SymbolId{0});
    if(count==800)Check(result.work<previous*3);previous=result.work;
    Check(std::holds_alternative<FlowConstants>(AnalyzeFlowConstants(flow,live,values,{result.allocated_bytes,result.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    flow.definitions.push_back({{},SymbolId{1},FlowDefinitionKind::kUndefined});values.push_back(FlowPrimitiveValue{UndefinedLiteral{},{}});live.live_definitions.push_back(0);
    const auto dead_initial=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));for(const auto& value:dead_initial.bindings)Check(value==SymbolId{0});
    live.live_definitions.back()=1;const auto initialized=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));
    Check(initialized.bindings[0]==SymbolId{0});for(unsigned i=1;i<count;++i)Check(!initialized.bindings[i]);live.live_definitions.back()=0;
    flow.bindings[0].stable_entry=false;const auto unavailable=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));for(const auto& value:unavailable.bindings)Check(!value);
    flow.bindings[0].stable_entry=true;flow.definitions.push_back({{},SymbolId{0},FlowDefinitionKind::kAssignment});values.push_back(UnknownFlowValue{});live.live_definitions.push_back(1);
    const auto written=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values));for(const auto& value:written.bindings)Check(!value);
    flow.bindings[0].kind=BindingKind::kLocal;Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values))==TreeStructureError::kBinding);
  }
  for(bool equal:{false,true})for(unsigned operation=0;operation<4;++operation)for(bool reverse:{false,true}) {
    RecoveryFlow flow{};flow.bindings.resize(3);FlowLiveness live{};live.live_definitions={1,1,1};
    for(unsigned i=0;i<3;++i){flow.bindings[i].kind=i<2?BindingKind::kParameter:BindingKind::kCompilerTemporary;flow.bindings[i].exposure=i<2?FlowBindingExposure::kObservable:FlowBindingExposure::kPrivate;flow.bindings[i].stable_entry=i<2;}
    const auto id=[&](unsigned i){return ExprId{reverse?3-i:i};};std::vector<FlowValueEquation> equations(4);
    equations[id(0).value]=UnknownFlowValue{};equations[id(1).value]=FlowBindingValue{{0}};equations[id(2).value]=FlowBindingValue{{equal?0U:1U}};
    if(!operation)equations[id(3).value]=FlowChoiceValue{id(0),id(1),id(2)};
    else equations[id(3).value]=FlowLogicalValue{operation==1?TreeLogical::kAnd:operation==2?TreeLogical::kOr:TreeLogical::kNullish,id(1),id(2)};
    flow.definitions={{{},{0},FlowDefinitionKind::kParameter},{{},{1},FlowDefinitionKind::kParameter},{{},{2},FlowDefinitionKind::kInitializer}};
    const std::vector<FlowDefinitionValue> values{UnknownFlowValue{},UnknownFlowValue{},FlowExpressionValue{id(3)}};
    const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values,equations,{},{}));Check(result.bindings[2]==(equal?std::optional{SymbolId{0}}:std::nullopt));
  }
  std::cout<<"independent stable binding value cases 32768\n";
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
void ExpressionConstants() {
  // Independent finite truth/selection table; no production transfer function,
  // lattice solver or source ID traversal is used to compute these answers.
  const std::vector<TreeLiteral> primitives{BooleanLiteral{false},BooleanLiteral{true},IntegerLiteral{42},IntegerLiteral{43},
      NullLiteral{},IntegerLiteral{0},NumberLiteral{std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN())},
      NumberLiteral{std::bit_cast<std::uint64_t>(-0.0)},TextId{0},TextId{1},UndefinedLiteral{}};
  const std::array<bool,11> truth{false,true,true,true,false,false,false,false,false,true,false};
  const std::array<bool,11> nullish{false,false,false,false,true,false,false,false,false,false,true};
  const std::vector<std::u16string> texts{u"",u"value"};
  std::size_t cases=0;
  for(unsigned condition=0;condition<=primitives.size();++condition)for(unsigned a=2;a<=4;++a)for(unsigned b=2;b<=4;++b)
    for(unsigned operation=0;operation<4;++operation)for(bool reverse:{false,true}) {
      RecoveryFlow flow{};flow.bindings.resize(1);flow.bindings[0].kind=BindingKind::kCompilerTemporary;flow.bindings[0].exposure=FlowBindingExposure::kPrivate;
      flow.definitions.push_back({{},{0},FlowDefinitionKind::kAssignment});FlowLiveness live{};live.live_definitions={1};
      std::vector<FlowValueEquation> equations(4);
      if(condition<primitives.size())equations[0]=FlowPrimitiveReference{{condition}};
      if(a<4)equations[1]=FlowPrimitiveReference{{a}};
      if(b<4)equations[2]=FlowPrimitiveReference{{b}};
      if(!operation)equations[3]=FlowChoiceValue{{0},{1},{2}};
      else equations[3]=FlowLogicalValue{operation==1?TreeLogical::kAnd:operation==2?TreeLogical::kOr:TreeLogical::kNullish,{0},{1}};
      std::optional<unsigned> expected;
      if(!operation) {
        if(condition<primitives.size()){const auto selected=truth[condition]?a:b;if(selected<4)expected=selected;}
        else if(a==b&&a<4)expected=a;
      }else if(condition<primitives.size()) {
        const bool right=operation==1?truth[condition]:operation==2?!truth[condition]:nullish[condition];
        if(!right)expected=condition;else if(a<4)expected=a;
      }
      std::vector<FlowDefinitionValue> values{FlowExpressionValue{{3}}};
      if(reverse) {
        const auto remap=[](ExprId id){return ExprId{3-id.value};};
        for(auto& equation:equations)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
          if constexpr(std::is_same_v<T,FlowChoiceValue>){value.condition=remap(value.condition);value.consequent=remap(value.consequent);value.alternate=remap(value.alternate);}
          else if constexpr(std::is_same_v<T,FlowLogicalValue>){value.left=remap(value.left);value.right=remap(value.right);}
        },equation);
        std::reverse(equations.begin(),equations.end());values[0]=FlowExpressionValue{{0}};
      }
      const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values,equations,primitives,texts));
      Check(bool(result.values[0])==bool(expected));if(expected)Check(result.values[0]->literal==PrimitiveId{*expected});
      // Same independent expected result through a borrowed current projection.
      // Terminal values now arise from binding facts, and a committed root
      // replacement owns the selection. The base root itself is unknown.
      auto base=equations;std::vector<FlowValueEquation> binding_values;
      const auto original=std::get<FlowExpressionValue>(values[0]).expression;
      const ExprId projected{static_cast<std::uint32_t>(base.size())};base.push_back(base[original.value]);
      base[original.value]=UnknownFlowValue{};
      for(auto& equation:base)if(const auto* literal=std::get_if<FlowPrimitiveReference>(&equation)) {
        const SymbolId binding{static_cast<std::uint32_t>(binding_values.size())};
        binding_values.push_back(*literal);equation=FlowBindingValue{binding};
      }
      std::vector<std::optional<ExprId>> replacements(base.size());replacements[original.value]=projected;
      const auto borrowed=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values,
          FlowValueEquations{base,replacements,binding_values},primitives,texts));
      Check(bool(borrowed.values[0])==bool(expected));if(expected)Check(borrowed.values[0]->literal==PrimitiveId{*expected});
      if(condition==0&&a==2&&b==2) {
        Check(std::holds_alternative<FlowConstants>(AnalyzeFlowConstants(flow,live,values,equations,primitives,texts,{result.allocated_bytes,result.work})));
        Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,equations,primitives,texts,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
        Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,equations,primitives,texts,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
      }
      ++cases;
    }
  // A temporarily known arm cannot hide an ungrounded competing source.
  RecoveryFlow flow{};flow.bindings.resize(2);
  for(auto& binding:flow.bindings){binding.kind=BindingKind::kCompilerTemporary;binding.exposure=FlowBindingExposure::kPrivate;}
  flow.definitions.push_back({{},{0},FlowDefinitionKind::kAssignment});FlowLiveness live{};live.live_definitions={1};
  const std::vector<FlowValueEquation> equations{UnknownFlowValue{},FlowPrimitiveReference{{2}},FlowBindingValue{{1}},FlowChoiceValue{{0},{1},{2}}};
  const std::vector<FlowDefinitionValue> values{FlowExpressionValue{{3}}};
  const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,values,equations,primitives,texts));
  Check(!result.values[0]&&!result.values[1]);
  const std::vector<std::optional<ExprId>> wrong_replacements(1);
  Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,values,
      FlowValueEquations{equations,wrong_replacements,{}},primitives,texts))==TreeStructureError::kOwnership);
  std::cout<<"independent expression equations "<<cases<<'\n';
}
void ReadConstants() {
  // Independent finite-set forward oracle. It recomputes incoming possibilities
  // on a three-point graph, then grounds empty cycles as unknown. No production
  // normal-value lattice or sparse dependency machinery computes the answer.
  const std::vector<TreeLiteral> primitives{IntegerLiteral{42},IntegerLiteral{43}};
  const std::vector<std::uint8_t> completed(3,1);
  std::size_t cases=0;
  for(bool symbolic:{false,true})for(unsigned edges=0;edges<512;++edges)for(unsigned actions=0;actions<64;++actions) {
    RecoveryFlow flow{};flow.events.resize(3);flow.bindings.resize(symbolic?3:1);
    if(symbolic)for(unsigned i=1;i<3;++i){flow.bindings[i].kind=BindingKind::kParameter;flow.bindings[i].exposure=FlowBindingExposure::kObservable;flow.bindings[i].stable_entry=true;}
    flow.bindings[0].exposure=FlowBindingExposure::kPrivate;
    std::vector<FlowDefinitionValue> definitions;std::vector<FlowValueEquation> equations(3,FlowBindingValue{{0}});
    std::array<unsigned,3> action{};auto encoded=actions;
    for(unsigned i=0;i<3;++i) {
      action[i]=encoded%4;encoded/=4;flow.events[i].read=SymbolId{0};
      flow.references.push_back({{i},{0},FlowReferenceKind::kRead,ExprId{i}});
      if(action[i]) {
        const auto id=static_cast<std::uint32_t>(definitions.size());
        flow.events[i].definition=FlowDefinitionId{id};flow.definitions.push_back({{i},{0},FlowDefinitionKind::kAssignment});
        if(action[i]==3)definitions.push_back(UnknownFlowValue{});
        else if(symbolic)definitions.push_back(FlowBindingValue{SymbolId{action[i]}});
        else definitions.push_back(FlowPrimitiveValue{primitives[action[i]-1],PrimitiveId{action[i]-1}});
      }
    }
    for(unsigned i=0;i<9;++i)if(edges&(1U<<i)){Link(flow,i/3,i%3);flow.edges.back().completion=i%2?FlowCompletion::kThrow:FlowCompletion::kResumeNext;}
    std::array<bool,3> reachable{true,false,false};bool growing=true;
    while(growing){growing=false;for(unsigned i=0;i<9;++i)if(edges&(1U<<i)){
      if(reachable[i/3]&&!reachable[i%3]){reachable[i%3]=true;growing=true;}
    }}
    std::array<unsigned,3> expected{};
    const auto close=[&] {
      bool changed=true;
      while(changed) {
        changed=false;
        for(unsigned i=0;i<3;++i) {
          unsigned incoming=i==0?1U:0U;bool predecessor=false;
          for(unsigned j=0;j<3;++j)if(reachable[j]&&(edges&(1U<<(j*3+i)))) {
            predecessor=true;incoming|=action[j]?action[j]==3?1U:1U<<action[j]:expected[j];
          }
          if(!predecessor)incoming|=1U;
          incoming|=expected[i];if(incoming!=expected[i]){expected[i]=incoming;changed=true;}
        }
      }
    };
    close();for(auto& state:expected)if(!state)state=1;close();
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
    for(unsigned i=0;i<3;++i)Check(bool(live.reachable[i])==reachable[i]);
    if(edges==2&&actions==1){
      const auto shared=std::get<FlowMotion>(AnalyzeFlowMotion(flow,live));
      const auto standalone=std::get<FlowMotion>(AnalyzeFlowMotion(flow));
      Check(shared.next==standalone.next&&shared.previous==standalone.previous);
      Check(std::holds_alternative<FlowMotion>(AnalyzeFlowMotion(flow,live,{shared.allocated_bytes,shared.work})));
      Check(std::get<TreeStructureError>(AnalyzeFlowMotion(flow,live,{shared.allocated_bytes-1,shared.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeFlowMotion(flow,live,{shared.allocated_bytes,shared.work-1}))==TreeStructureError::kWorkLimit);
      auto invalid=live;invalid.reachable.pop_back();
      Check(std::get<TreeStructureError>(AnalyzeFlowMotion(flow,invalid))==TreeStructureError::kOwnership);
    }
    const auto result=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,definitions,
        FlowValueEquations{equations,{},{},completed,{}},primitives,{}));
    for(unsigned i=0;i<3;++i) {
      const bool known=reachable[i]&&(expected[i]==2||expected[i]==4);
      if(symbolic){
        const auto found=std::find_if(result.binding_reads.begin(),result.binding_reads.end(),[&](const auto& read){return read.expression==ExprId{i};});
        Check((found!=result.binding_reads.end())==known);
        if(known)Check(found->binding==SymbolId{expected[i]==2?1U:2U});
        Check(result.reads.empty());
      }else{
        const auto found=std::find_if(result.reads.begin(),result.reads.end(),[&](const auto& read){return read.expression==ExprId{i};});
        Check((found!=result.reads.end())==known);
        if(known)Check(found->primitive==PrimitiveId{expected[i]==2?0U:1U});
        Check(result.binding_reads.empty());
      }
    }
    if(edges==2&&actions==1) {
      Check(std::holds_alternative<FlowConstants>(AnalyzeFlowConstants(flow,live,definitions,FlowValueEquations{equations,{},{},completed,{}},primitives,{}, {result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,definitions,FlowValueEquations{equations,{},{},completed,{}},primitives,{}, {result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeFlowConstants(flow,live,definitions,FlowValueEquations{equations,{},{},completed,{}},primitives,{}, {result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
      const std::vector<std::uint8_t> unavailable(3,0);
      const auto missing=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,definitions,FlowValueEquations{equations,{},{},unavailable,{}},primitives,{}));
      Check(missing.reads.empty()&&missing.binding_reads.empty());
      flow.bindings[0].exposure=FlowBindingExposure::kObservable;
      const auto exposed=std::get<FlowConstants>(AnalyzeFlowConstants(flow,live,definitions,FlowValueEquations{equations,{},{},completed,{}},primitives,{}));
      Check(exposed.reads.empty()&&exposed.binding_reads.empty());
      if(symbolic){flow.bindings[0].exposure=FlowBindingExposure::kPrivate;
        flow.definitions.push_back({{},SymbolId{1},FlowDefinitionKind::kAssignment});definitions.push_back(UnknownFlowValue{});
        auto written_live=live;written_live.live_definitions.push_back(1);
        const auto written=std::get<FlowConstants>(AnalyzeFlowConstants(flow,written_live,definitions,FlowValueEquations{equations,{},{},completed,{}},primitives,{}));
        Check(written.binding_reads.empty());
      }
    }
    ++cases;
  }
  std::cout<<"independent per-read normal values "<<cases<<'\n';
}
void InitializedRepresentatives() {
  std::array<StorageBinding,2> bindings{};BindingInterference facts;
  for(auto& binding:bindings){binding.eligible=true;binding.binding_region=SourceRegion{{0},{10}};binding.reference_region=SourceRegion{{2},{3}};}
  bindings[0].initialized_region=CompletionRegion{1,5};bindings[0].reachable_references=CompletionRegion{1,2};
  bindings[1].native_declaration=true;bindings[1].initialized_region=CompletionRegion{3,5};bindings[1].reachable_references=CompletionRegion{3,4};
  facts.affinities.push_back({{0},{1},false});
  // Native spelling cannot make a later initialization cover earlier writes.
  auto plan=std::get<StorageCoalescing>(PlanStorageCoalescing(facts,bindings));
  Check(!plan.storage[0]);Check(plan.storage[1]==SymbolId{0});
  // Unreachable syntax has no completion-order demand; source scope remains.
  bindings[0].reachable_references.reset();
  plan=std::get<StorageCoalescing>(PlanStorageCoalescing(facts,bindings));
  Check(plan.storage[0]==SymbolId{1});Check(!plan.storage[1]);
}
void GroupRepresentatives() {
  // A candidate discarded in an early contraction can be the only slot whose
  // known scope covers the final group. Source placement must not depend on
  // affinity order or on which component root survived DSU balancing.
  std::uint64_t small=0,large=0;
  for(unsigned count:{3U,200U,201U,400U,800U})for(bool reverse:{false,true})
    for(bool reordered:{false,true})for(bool projected:{false,true}) {
      const auto id=[&](unsigned i){return SymbolId{reverse?count-1-i:i};};
      const auto outer=id(0);std::vector<StorageBinding> bindings(count);
      BindingInterference facts;
      for(unsigned i=0;i<count;++i) {
        auto& binding=bindings[id(i).value];binding.eligible=true;
        binding.native_declaration=i!=0;binding.initialization_order=i+1;
        binding.reference_region=i+1==count?SourceRegion{{6},{7}}:SourceRegion{{2},{3}};
        if(i)binding.binding_region=i+1==count?SourceRegion{{6},{8}}:SourceRegion{{2},{4}};
        else if(projected)binding.binding_region=SourceRegion{{0},{10}};
        if(i)facts.affinities.push_back({id(i-1),id(i),false});
      }
      if(reordered)std::reverse(facts.affinities.begin(),facts.affinities.end());
      const auto result=PlanStorageCoalescing(facts,bindings);
      Check(std::holds_alternative<StorageCoalescing>(result));
      const auto& plan=std::get<StorageCoalescing>(result);
      if(!reverse&&!reordered&&!projected){if(count==400)small=plan.work;if(count==800)large=plan.work;}
      for(unsigned i=0;i<count;++i) {
        if(plan.storage[i]!=(i==outer.value?std::nullopt:std::optional{outer}))
          std::cerr<<"group representative count="<<count<<" reverse="<<reverse<<" reordered="<<reordered<<" projected="<<projected<<'\n';
        Check(plan.storage[i]==(i==outer.value?std::nullopt:std::optional{outer}));
      }
      if(count<=201) {
        Check(std::holds_alternative<StorageCoalescing>(PlanStorageCoalescing(facts,bindings,{plan.allocated_bytes,plan.work})));
        Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,bindings,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
        Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,bindings,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
      }
    }
  Check(large<small*3);
}
void StorageScopePreference() {
  // A native narrow slot must not win merely because placement ran first.
  // A broader representative is only a proposal; source integration still
  // proves completion, spelling and activation for every remapped reference.
  for(unsigned count:{2U,200U,201U,400U,800U})for(bool reverse:{false,true})
    for(bool projected:{false,true}) {
      std::vector<StorageBinding> bindings(count);
      const SymbolId outer{reverse?count-1:0};BindingInterference facts;
      for(unsigned i=0;i<count;++i) {
        bindings[i].eligible=true;bindings[i].native_declaration=i!=outer.value;
        bindings[i].initialization_order=i==outer.value?2:1;
        if(i!=outer.value)bindings[i].binding_region=SourceRegion{{2},{4}};
        else if(projected)bindings[i].binding_region=SourceRegion{{0},{6}};
        bindings[i].reference_region=i==outer.value?SourceRegion{{0},{6}}:SourceRegion{{2},{4}};
        if(i!=outer.value)facts.affinities.push_back({SymbolId{i},outer,false});
      }
      const auto result=PlanStorageCoalescing(facts,bindings);
      Check(std::holds_alternative<StorageCoalescing>(result));
      const auto& plan=std::get<StorageCoalescing>(result);
      for(unsigned i=0;i<count;++i)Check(plan.storage[i]==(i==outer.value?std::nullopt:std::optional{outer}));
      Check(std::holds_alternative<StorageCoalescing>(PlanStorageCoalescing(facts,bindings,{plan.allocated_bytes,plan.work})));
      Check(std::get<TreeStructureError>(PlanStorageCoalescing(facts,bindings,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    }
  std::array<StorageBinding,2> bindings{};BindingInterference facts;
  for(auto& binding:bindings){binding.eligible=true;binding.reference_region=SourceRegion{{2},{4}};}
  bindings[1].native_declaration=true;bindings[1].binding_region=SourceRegion{{2},{4}};
  facts.affinities.push_back({{0},{1},false});
  // Both candidates cover all references: keep the native narrow spelling.
  const auto plan=std::get<StorageCoalescing>(PlanStorageCoalescing(facts,bindings));
  Check(plan.storage[0]==SymbolId{1});Check(!plan.storage[1]);
}
void SnapshotCheckpoints() {
  const auto rejects=[](const auto& action){try{action();return false;}catch(TreeStructureError error){return error==TreeStructureError::kOwnership;}};
  const auto cleanup=[](RecoverySchedule& schedule){for(const auto pass:{RecoveryPass::kDeadDefinitions,RecoveryPass::kInitialValues,RecoveryPass::kBindings}){schedule.Start(pass);Check(!schedule.Finish());}};
  for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool changes:{false,true}) {
    RecoverySchedule schedule;Check(rejects([&]{(void)schedule.SnapshotRefreshNeeded();}));cleanup(schedule);schedule.EnterStorage();
    Check(schedule.SnapshotRefreshNeeded());schedule.Start(RecoveryPass::kSnapshots);Check(!schedule.Finish());Check(!schedule.SnapshotRefreshNeeded());
    for(unsigned i=0;i<count;++i) {
      schedule.Start(RecoveryPass::kStorage);if(changes)Check(schedule.Commit());Check(schedule.Finish()==changes);
      if(changes)Check(rejects([&]{(void)schedule.SnapshotRefreshNeeded();}));cleanup(schedule);
      Check(schedule.SnapshotRefreshNeeded()==changes);
      schedule.Start(RecoveryPass::kAliases);Check(!schedule.Finish());Check(schedule.SnapshotRefreshNeeded()==changes);
      if(schedule.SnapshotRefreshNeeded()) {
        const auto previous=schedule.Revision();schedule.Start(RecoveryPass::kSnapshots);
        Check(rejects([&]{(void)schedule.SnapshotRefreshNeeded();}));Check(schedule.Commit());Check(schedule.Finish());
        Check(rejects([&]{schedule.RequireCurrent(previous);}));Check(!schedule.DemandCleanupNeeded());Check(!schedule.SnapshotRefreshNeeded());
      }
    }
    Check(schedule.Revision().value==(changes?count*2:0));schedule.EnterReadValues();schedule.Start(RecoveryPass::kBindingValues);Check(!schedule.Finish());schedule.EnterGrammar();Check(rejects([&]{(void)schedule.SnapshotRefreshNeeded();}));
  }
  RecoverySchedule dirty;cleanup(dirty);dirty.EnterStorage();dirty.Start(RecoveryPass::kInertOperands);Check(dirty.Commit());Check(dirty.Finish());
  Check(rejects([&]{(void)dirty.SnapshotRefreshNeeded();}));dirty.Start(RecoveryPass::kDeadDefinitions);Check(!dirty.Finish());
  Check(rejects([&]{(void)dirty.SnapshotRefreshNeeded();}));dirty.Start(RecoveryPass::kInitialValues);Check(!dirty.Finish());
  Check(rejects([&]{(void)dirty.SnapshotRefreshNeeded();}));dirty.Start(RecoveryPass::kBindings);Check(!dirty.Finish());Check(dirty.SnapshotRefreshNeeded());
  // Even an unchanged projection must finish every ordered cleanup owner before
  // querying a stable consumer boundary; an older clean checkpoint is not enough.
  RecoverySchedule clean;cleanup(clean);clean.EnterStorage();clean.Start(RecoveryPass::kDeadDefinitions);Check(!clean.Finish());
  Check(rejects([&]{(void)clean.SnapshotRefreshNeeded();}));clean.Start(RecoveryPass::kInitialValues);Check(!clean.Finish());
  Check(rejects([&]{(void)clean.SnapshotRefreshNeeded();}));clean.Start(RecoveryPass::kBindings);Check(!clean.Finish());Check(clean.SnapshotRefreshNeeded());
  std::cout<<"snapshot checkpoint revision/cleanup/stage counterexamples passed\n";
}
void AvailableCopyOracle() {
  std::uint64_t cases=0;
  // Independent finite concrete state exploration. Roots denote distinct
  // entry values; each source write creates a fresh value. The state records
  // which current root the temporary equals, with no production lattice,
  // sparse facts, dependency graph or transfer helper in the oracle.
  for(unsigned mask=0;mask<512;++mask)for(unsigned encoded=0;encoded<216;++encoded)for(bool reverse:{false,true}) {
    const auto id=[&](unsigned i){return SymbolId{reverse?2-i:i};};
    RecoveryFlow flow{};flow.events.resize(3);flow.bindings.resize(3);flow.entry={0};
    flow.bindings[id(0).value].kind=BindingKind::kCompilerTemporary;flow.bindings[id(0).value].exposure=FlowBindingExposure::kPrivate;
    for(unsigned root=1;root<=2;++root){flow.bindings[id(root).value].kind=BindingKind::kParameter;flow.bindings[id(root).value].stable_entry=(mask&(1U<<(root-1)))!=0;}
    std::array<unsigned,3> operations{};auto code=encoded;
    std::array<FlowValueEquation,3> equations;std::array<std::uint8_t,3> completed{1,std::uint8_t(encoded%2),1};
    for(unsigned point=0;point<3;++point) {
      operations[point]=code%6;code/=6;flow.events[point].read=id(0);equations[point]=FlowBindingValue{id(0)};
      // A reference inside an update is not a replaceable SymbolExpr read.
      if(point==1&&encoded%5==0)equations[point]=UnknownFlowValue{};
      flow.references.push_back({{point},id(0),FlowReferenceKind::kRead,ExprId{point}});
      if(operations[point]) {
        const auto definition=FlowDefinitionId{static_cast<std::uint32_t>(flow.definitions.size())};flow.events[point].definition=definition;
        const bool root_write=operations[point]==3||operations[point]==4;
        const auto target=id(root_write?operations[point]-2:0);
        const auto kind=root_write?FlowDefinitionKind::kUpdate:operations[point]==5?FlowDefinitionKind::kUndefined:FlowDefinitionKind::kAssignment;
        flow.definitions.push_back({{point},target,kind,operations[point]<=2?std::optional{id(operations[point])}:std::nullopt});
      }
    }
    for(unsigned from=0;from<3;++from)for(unsigned to=0;to<3;++to)if(mask&(1U<<(from*3+to)))Link(flow,from,to);
    std::array<std::array<bool,3>,3> seen{};std::vector<std::pair<unsigned,unsigned>> pending{{0,0}};seen[0][0]=true;
    while(!pending.empty()) {
      const auto [point,input]=pending.back();pending.pop_back();auto output=input;
      switch(operations[point]) {
        case 0:break;case 1:output=1;break;case 2:output=2;break;
        case 3:if(input==1)output=0;break;case 4:if(input==2)output=0;break;
        case 5:output=0;break;
      }
      for(unsigned target=0;target<3;++target)if((mask&(1U<<(point*3+target)))&&!seen[target][output]){seen[target][output]=true;pending.push_back({target,output});}
    }
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
    const FlowValueEquations expressions{equations,{},{},completed};
    const auto result=std::get<FlowCopies>(AnalyzeFlowCopies(flow,live,expressions));
    std::array<std::optional<SymbolId>,3> actual{};for(const auto& read:result.reads)actual[read.expression.value]=read.source;
    for(unsigned point=0;point<3;++point) {
      std::optional<SymbolId> expected;
      if(completed[point]&&std::holds_alternative<FlowBindingValue>(equations[point])&&!seen[point][0]) {
        if(seen[point][1]&&!seen[point][2]&&flow.bindings[id(1).value].stable_entry)expected=id(1);
        if(seen[point][2]&&!seen[point][1]&&flow.bindings[id(2).value].stable_entry)expected=id(2);
      }
      Check(actual[point]==expected);
    }
    if(!mask&&encoded==1&&!reverse) {
      Check(std::holds_alternative<FlowCopies>(AnalyzeFlowCopies(flow,live,expressions,{result.allocated_bytes,result.work})));
      Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,live,expressions,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,live,expressions,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
      auto invalid=live;invalid.reachable.pop_back();Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,invalid,expressions))==TreeStructureError::kOwnership);
      auto short_reads=expressions;short_reads.completed_reads=std::span{completed}.first(2);Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,live,short_reads))==TreeStructureError::kOwnership);
      auto invalid_flow=flow;invalid_flow.definitions[0].point={3};Check(std::get<TreeStructureError>(AnalyzeFlowCopies(invalid_flow,live,expressions))==TreeStructureError::kOwnership);
      invalid_flow=flow;invalid_flow.definitions[0].copy=id(3);Check(std::get<TreeStructureError>(AnalyzeFlowCopies(invalid_flow,live,expressions))==TreeStructureError::kBinding);
    }
    ++cases;
  }
  // A write reference without a modeled normal value still changes storage.
  // Test both destination and source kills; never infer an unchanged value
  // from the absence of a FlowDefinition.
  for(unsigned target:{0U,1U}) {
    RecoveryFlow flow{};flow.events.resize(3);flow.bindings.resize(2);flow.entry={0};
    flow.bindings[0].kind=BindingKind::kCompilerTemporary;flow.bindings[0].exposure=FlowBindingExposure::kPrivate;
    flow.bindings[1].kind=BindingKind::kParameter;flow.bindings[1].stable_entry=true;
    flow.definitions={{{0},{0},FlowDefinitionKind::kAssignment,SymbolId{1}}};flow.events[0].definition=FlowDefinitionId{0};
    Link(flow,0,1);Link(flow,1,2);flow.events[2].read=SymbolId{0};
    flow.references={{{1},{target},FlowReferenceKind::kWrite,StmtId{0}},{{2},{0},FlowReferenceKind::kRead,ExprId{0}}};
    const std::array<FlowValueEquation,1> equations{FlowBindingValue{{0}}};const std::array<std::uint8_t,1> completed{1};
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
    Check(std::get<FlowCopies>(AnalyzeFlowCopies(flow,live,{equations,{},{},completed})).reads.empty());
  }
  std::uint64_t small=0,large=0;
  for(unsigned count:{200U,201U,400U,800U}) {
    RecoveryFlow flow{};flow.events.resize(count);flow.bindings.resize(2);flow.entry={0};
    flow.bindings[0].kind=BindingKind::kCompilerTemporary;flow.bindings[0].exposure=FlowBindingExposure::kPrivate;
    flow.bindings[1].kind=BindingKind::kParameter;flow.bindings[1].stable_entry=true;
    flow.definitions={{{0},{0},FlowDefinitionKind::kAssignment,SymbolId{1}},{{count/2},{1},FlowDefinitionKind::kUpdate}};
    flow.events[0].definition=FlowDefinitionId{0};flow.events[count/2].definition=FlowDefinitionId{1};
    std::vector<FlowValueEquation> equations(count,FlowBindingValue{{0}});std::vector<std::uint8_t> completed(count,1);
    for(unsigned i=1;i<count;++i){Link(flow,i-1,i);flow.events[i].read=SymbolId{0};flow.references.push_back({{i},{0},FlowReferenceKind::kRead,ExprId{i}});}
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));const FlowValueEquations expressions{equations,{},{},completed};
    const auto result=std::get<FlowCopies>(AnalyzeFlowCopies(flow,live,expressions));Check(result.reads.size()==count/2);
    for(unsigned i=0;i<result.reads.size();++i)Check(result.reads[i].expression==ExprId{i+1}&&result.reads[i].source==SymbolId{1});
    Check(std::holds_alternative<FlowCopies>(AnalyzeFlowCopies(flow,live,expressions,{result.allocated_bytes,result.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,live,expressions,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,live,expressions,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    if(count==400)small=result.work;if(count==800)large=result.work;
  }
  Check(large<small*3);std::cout<<"independent current-copy path states "<<cases<<'\n';
}
void TransitiveCopyOracle() {
  enum class Action : unsigned {kNone,kLeftFirst,kLeftSecond,kRightLeft,kLeftRight,kWriteFirst,kWriteSecond,kClearRight};
  constexpr unsigned actions=8;std::uint64_t cases=0;
  // Concrete finite states retain both private cells' equality with the two
  // current entry values. Copying a private cell transfers its value; changing
  // it cannot retroactively change the other cell's saved value. No production
  // lattice, live fact, query seed or dependency helper computes expectations.
  for(unsigned mask=0;mask<512;++mask)for(unsigned encoded=0;encoded<actions*actions*actions;++encoded)for(bool reverse:{false,true}) {
    const auto id=[&](unsigned i){return SymbolId{reverse?3-i:i};};
    RecoveryFlow flow{};flow.events.resize(3);flow.bindings.resize(4);flow.entry={0};
    for(unsigned cell=0;cell<2;++cell){flow.bindings[id(cell).value].kind=BindingKind::kCompilerTemporary;flow.bindings[id(cell).value].exposure=FlowBindingExposure::kPrivate;}
    for(unsigned root=2;root<4;++root){flow.bindings[id(root).value].kind=BindingKind::kParameter;flow.bindings[id(root).value].stable_entry=(mask&(1U<<(root-2)))!=0;}
    std::array<Action,3> operations{};auto code=encoded;
    std::array<FlowValueEquation,3> equations;std::array<std::uint8_t,3> completed{1,std::uint8_t(encoded%2),1};
    for(unsigned point=0;point<3;++point) {
      operations[point]=static_cast<Action>(code%actions);code/=actions;flow.events[point].read=id(point%2);equations[point]=FlowBindingValue{id(point%2)};
      if(point==1&&encoded%5==0)equations[point]=UnknownFlowValue{};
      flow.references.push_back({{point},id(point%2),FlowReferenceKind::kRead,ExprId{point}});
      std::optional<unsigned> target,source;auto kind=FlowDefinitionKind::kAssignment;
      switch(operations[point]) {
        case Action::kNone:break;
        case Action::kLeftFirst:target=0;source=2;break;case Action::kLeftSecond:target=0;source=3;break;
        case Action::kRightLeft:target=1;source=0;break;case Action::kLeftRight:target=0;source=1;break;
        case Action::kWriteFirst:target=2;kind=FlowDefinitionKind::kUpdate;break;
        case Action::kWriteSecond:target=3;kind=FlowDefinitionKind::kUpdate;break;
        case Action::kClearRight:target=1;kind=FlowDefinitionKind::kUndefined;break;
      }
      if(target){flow.events[point].definition=FlowDefinitionId{static_cast<std::uint32_t>(flow.definitions.size())};flow.definitions.push_back({{point},id(*target),kind,source?std::optional{id(*source)}:std::nullopt});}
    }
    for(unsigned from=0;from<3;++from)for(unsigned to=0;to<3;++to)if(mask&(1U<<(from*3+to)))Link(flow,from,to);
    std::array<std::array<bool,9>,3> seen{};std::vector<std::pair<unsigned,unsigned>> pending{{0,0}};seen[0][0]=true;
    while(!pending.empty()) {
      const auto [point,state]=pending.back();pending.pop_back();unsigned left=state%3,right=state/3;
      switch(operations[point]) {
        case Action::kNone:break;case Action::kLeftFirst:left=1;break;case Action::kLeftSecond:left=2;break;
        case Action::kRightLeft:right=left;break;case Action::kLeftRight:left=right;break;
        case Action::kWriteFirst:if(left==1)left=0;if(right==1)right=0;break;
        case Action::kWriteSecond:if(left==2)left=0;if(right==2)right=0;break;
        case Action::kClearRight:right=0;break;
      }
      const auto output=left+3*right;
      for(unsigned target=0;target<3;++target)if((mask&(1U<<(point*3+target)))&&!seen[target][output]){seen[target][output]=true;pending.push_back({target,output});}
    }
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));const FlowValueEquations expressions{equations,{},{},completed};
    const auto result=std::get<FlowCopies>(AnalyzeFlowCopies(flow,live,expressions));std::array<std::optional<SymbolId>,3> actual{};
    for(const auto& read:result.reads)actual[read.expression.value]=read.source;
    for(unsigned point=0;point<3;++point) {
      std::array<bool,3> values{};for(unsigned state=0;state<9;++state)if(seen[point][state])values[point%2?state/3:state%3]=true;
      std::optional<SymbolId> expected;
      if(completed[point]&&std::holds_alternative<FlowBindingValue>(equations[point])&&!values[0]) {
        if(values[1]&&!values[2]&&flow.bindings[id(2).value].stable_entry)expected=id(2);
        if(values[2]&&!values[1]&&flow.bindings[id(3).value].stable_entry)expected=id(3);
      }
      Check(actual[point]==expected);
    }
    ++cases;
  }
  // Completion queries support proofs but cannot keep an unobserved copy alive.
  RecoveryFlow query{};query.events.resize(3);query.bindings.resize(3);query.entry={0};Link(query,0,1);Link(query,1,2);
  query.definitions={{{0},{0},FlowDefinitionKind::kAssignment,SymbolId{2}},{{1},{1},FlowDefinitionKind::kAssignment,SymbolId{0}}};
  query.events[0].definition=FlowDefinitionId{0};query.events[1].definition=FlowDefinitionId{1};
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(query));
  Check(std::binary_search(live.live_in.begin(),live.live_in.end(),LiveBinding{{1},{0}}));Check(live.live_definitions[0]);
  const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(query));Check(demand.definitions==std::vector<std::uint8_t>({0,0}));
  auto invalid=query;invalid.definitions[0].copy=SymbolId{3};Check(std::get<TreeStructureError>(AnalyzeFlowLiveness(invalid))==TreeStructureError::kOwnership);
  invalid=query;invalid.definitions[0].point=FlowPoint{3};Check(std::get<TreeStructureError>(AnalyzeFlowLiveness(invalid))==TreeStructureError::kOwnership);
  for(bool split:{false,true})for(bool reverse:{false,true}) {
  std::uint64_t previous=0;
  for(unsigned count:{200U,201U,400U,800U}) {
    RecoveryFlow flow{};flow.events.resize(count+1);flow.bindings.resize(count+1);flow.entry={0};
    flow.bindings[0].kind=BindingKind::kParameter;flow.bindings[0].stable_entry=true;
    for(unsigned i=1;i<=count;++i){flow.bindings[i].kind=BindingKind::kCompilerTemporary;flow.bindings[i].exposure=FlowBindingExposure::kPrivate;Link(flow,i-1,i);
      flow.events[i-1].definition=FlowDefinitionId{i-1};flow.definitions.push_back({{i-1},{i},FlowDefinitionKind::kAssignment,SymbolId{i-1}});}
    if(split){
      for(unsigned i=1;i<=count;++i)flow.events.emplace_back();
      flow.edges.clear();for(auto& event:flow.events){event.incoming.reset();event.outgoing.reset();}
      for(unsigned i=1;i<=count;++i){Link(flow,i-1,count+i);Link(flow,count+i,i);}
    }
    auto read_point=FlowPoint{count};
    if(reverse){
      const auto mapped=[&](FlowPoint point){return FlowPoint{static_cast<std::uint32_t>(flow.events.size()-1-point.value)};};
      for(auto& edge:flow.edges){edge.from=mapped(edge.from);edge.to=mapped(edge.to);}
      for(auto& definition:flow.definitions)definition.point=mapped(definition.point);
      flow.entry=mapped(flow.entry);read_point=mapped(read_point);std::reverse(flow.events.begin(),flow.events.end());
    }
    flow.events[read_point.value].read=SymbolId{count};flow.references={{{read_point},{count},FlowReferenceKind::kRead,ExprId{0}}};
    const std::array<FlowValueEquation,1> equations{FlowBindingValue{{count}}};const std::array<std::uint8_t,1> completed{1};
    const auto lives=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));const FlowValueEquations expressions{equations,{},{},completed};
    const auto result=std::get<FlowCopies>(AnalyzeFlowCopies(flow,lives,expressions));Check(result.reads.size()==1&&result.reads[0].source==SymbolId{0});
    Check(std::holds_alternative<FlowCopies>(AnalyzeFlowCopies(flow,lives,expressions,{result.allocated_bytes,result.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,lives,expressions,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowCopies(flow,lives,expressions,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
    Check(std::holds_alternative<FlowLiveness>(AnalyzeFlowLiveness(flow,{lives.allocated_bytes,lives.work})));
    Check(std::get<TreeStructureError>(AnalyzeFlowLiveness(flow,{lives.allocated_bytes-1,lives.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(AnalyzeFlowLiveness(flow,{lives.allocated_bytes,lives.work-1}))==TreeStructureError::kWorkLimit);
    const auto work=result.work+lives.work;if(count>=400)Check(work<previous*3);previous=work;
    // An exposed intermediate can change independently of graph writes. It
    // cannot transfer an entry-root proof through an unmodeled external cell.
    flow.bindings[count/2].exposure=FlowBindingExposure::kObservable;
    Check(std::get<FlowCopies>(AnalyzeFlowCopies(flow,lives,expressions)).reads.empty());
  }
  }
  std::cout<<"independent transitive-copy path states "<<cases<<'\n';
}
void InitializationTransfers() {
  const auto rejects=[](const auto& action) {
    try {action();return false;}catch(TreeStructureError error){return error==TreeStructureError::kOwnership;}
  };
  const auto cleanup=[](RecoverySchedule& schedule) {
    for(const auto pass:{RecoveryPass::kDeadDefinitions,RecoveryPass::kInitialValues,RecoveryPass::kBindings}) {
      schedule.Start(pass);Check(!schedule.Finish());
    }
  };
  // Independently specified obligations: no test expectation calls PassContract.
  // A wrong preservation flag or unchecked phase/version must fail this table.
  const std::array obligations{
      std::pair{RecoveryPass::kDeclarations,false},std::pair{RecoveryPass::kConstants,false},
      std::pair{RecoveryPass::kBindingValues,false},std::pair{RecoveryPass::kAliases,false},
      std::pair{RecoveryPass::kStoredResults,true},std::pair{RecoveryPass::kDeadDefinitions,true},
      std::pair{RecoveryPass::kInitialValues,true},std::pair{RecoveryPass::kBindings,true},
      std::pair{RecoveryPass::kSnapshots,false},std::pair{RecoveryPass::kStorage,true},
      std::pair{RecoveryPass::kInertStatements,true},std::pair{RecoveryPass::kInertOperands,true},
      std::pair{RecoveryPass::kLoopDeclarations,false},std::pair{RecoveryPass::kDeclarationKinds,true}};
  for(const auto [pass,preserves]:obligations)for(bool commit:{false,true}) {
    RecoverySchedule schedule;
    if(pass==RecoveryPass::kDeclarations||pass==RecoveryPass::kAliases)cleanup(schedule);
    if(pass==RecoveryPass::kSnapshots||pass==RecoveryPass::kStorage||
        pass==RecoveryPass::kBindingValues||pass==RecoveryPass::kLoopDeclarations||pass==RecoveryPass::kDeclarationKinds) {
      cleanup(schedule);schedule.EnterStorage();
      if(pass!=RecoveryPass::kSnapshots&&pass!=RecoveryPass::kStorage) {
        schedule.Start(RecoveryPass::kSnapshots);Check(!schedule.Finish());schedule.EnterReadValues();
        if(pass!=RecoveryPass::kBindingValues) {
          schedule.Start(RecoveryPass::kBindingValues);Check(!schedule.Finish());schedule.EnterGrammar();
        }
      }
    }
    if(pass==RecoveryPass::kInitialValues||pass==RecoveryPass::kBindings) {
      schedule.Start(RecoveryPass::kDeadDefinitions);Check(!schedule.Finish());
      if(pass==RecoveryPass::kBindings){schedule.Start(RecoveryPass::kInitialValues);Check(!schedule.Finish());}
    }
    const auto source=schedule.Revision();
    Check(rejects([&]{(void)schedule.TransportInitializations(source);}));
    schedule.Start(pass);schedule.RequireCurrent(source);
    Check(rejects([&]{(void)schedule.TransportInitializations(source);}));
    if(commit) {
      Check(schedule.Commit());const auto target=schedule.Revision();
      Check(target.value==source.value+1);
      const auto transported=schedule.TransportInitializations(source);
      Check(transported.has_value()==preserves);
      if(transported)Check(transported->value==target.value);
      Check(rejects([&]{(void)schedule.TransportInitializations(target);}));
      Check(rejects([&]{(void)schedule.TransportInitializations({target.value+1});}));
      Check(rejects([&]{schedule.RequireCurrent(target);})); // No reads in commit, even transported ones.
      Check(!schedule.Commit()); // Further writes do not create another evidence revision.
      Check(schedule.TransportInitializations(source).has_value()==preserves);
      Check(schedule.Finish());schedule.RequireCurrent(target);
      Check(rejects([&]{schedule.RequireCurrent(source);}));
    }else{Check(!schedule.Finish());schedule.RequireCurrent(source);}
    Check(rejects([&]{(void)schedule.TransportInitializations(source);}));
  }
  for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
    RecoverySchedule schedule;auto proof=schedule.Revision();
    for(unsigned i=0;i<count;++i) {
      // Consecutive preserving consumers must transport from the last proved
      // input, not from the immutable original CFG revision or a batch output.
      schedule.Start(i%2?RecoveryPass::kStoredResults:RecoveryPass::kInertOperands);
      schedule.RequireCurrent(proof);Check(schedule.Commit());
      const auto next=schedule.TransportInitializations(proof);Check(next.has_value());
      if(i)Check(rejects([&]{(void)schedule.TransportInitializations({proof.value-1});}));
      proof=*next;Check(!schedule.Commit());Check(schedule.Finish());schedule.RequireCurrent(proof);
    }
    Check(proof.value==count);
    schedule.Start(RecoveryPass::kConstants);Check(schedule.Commit());
    Check(!schedule.TransportInitializations(proof));Check(schedule.Finish());
    Check(rejects([&]{schedule.RequireCurrent(proof);}));
  }
  std::cout<<"initialization evidence phase/revision/transport counterexamples passed\n";
}
void PassContracts() {
  const auto rejects=[](const auto& action) {
    try {action();return false;}catch(TreeStructureError error){return error==TreeStructureError::kOwnership;}
  };
  const auto cleanup=[](RecoverySchedule& schedule) {
    for(const auto pass:{RecoveryPass::kDeadDefinitions,RecoveryPass::kInitialValues,RecoveryPass::kBindings}) {
      schedule.Start(pass);Check(!schedule.Finish());
    }
  };
  // These are ownership/order counterexamples, independent of source layout.
  // A proposal can only read its declared facts on its unchanged projection.
  RecoverySchedule schedule;
  schedule.Start(RecoveryPass::kStoredResults);
  const auto original=schedule.Revision();
  schedule.RequireFact(RecoveryFact::kExpressions);
  schedule.RequireFact(RecoveryFact::kFlow); // Joint expression/flow bootstrap.
  // Preservation is a commit-time proof transfer, never permission to reuse
  // an analysis while the source/destination revisions are unspecified.
  Check(rejects([&]{(void)schedule.TransportInitializations(original);}));
  Check(rejects([&]{schedule.RequireFact(RecoveryFact::kConstants);}));
  Check(rejects([&]{schedule.RequireFact(RecoveryFact::kCopies);}));
  Check(rejects([&]{schedule.Start(RecoveryPass::kAliases);}));
  Check(schedule.Commit());Check(!schedule.Commit());
  Check(rejects([&]{schedule.RequireFact(RecoveryFact::kExpressions);}));
  Check(rejects([&]{schedule.RequireCurrent(original);}));
  Check(schedule.Finish());
  Check(schedule.Revision().value==original.value+1);
  Check(rejects([&]{schedule.RequireCurrent(original);})); // Stale even after commit has finished.
  Check(rejects([&]{schedule.Start(RecoveryPass::kAliases);}));
  Check(rejects([&]{schedule.EnterStorage();}));
  schedule.Start(RecoveryPass::kConstants);
  schedule.RequireFact(RecoveryFact::kConstants);
  schedule.RequireFact(RecoveryFact::kLiveness);
  schedule.RequireCurrent(schedule.Revision());
  Check(!schedule.Finish()); // No proposal: neither revision nor cleanup changes.
  Check(schedule.DemandCleanupNeeded());cleanup(schedule);
  Check(!schedule.DemandCleanupNeeded());
  schedule.Start(RecoveryPass::kAliases);Check(!schedule.Finish());
  Check(!schedule.DemandCleanupNeeded());
  Check(rejects([&]{schedule.Start(RecoveryPass::kLoopDeclarations);}));
  Check(rejects([&]{schedule.Start(RecoveryPass::kBindingValues);}));
  schedule.EnterStorage();
  Check(rejects([&]{schedule.EnterReadValues();}));
  Check(rejects([&]{schedule.Start(RecoveryPass::kConstants);}));Check(rejects([&]{schedule.Start(RecoveryPass::kBindingValues);}));
  // Snapshot motion preserves the demand checkpoint only when one exists.
  schedule.Start(RecoveryPass::kSnapshots);Check(schedule.Commit());Check(schedule.Finish());
  Check(!schedule.DemandCleanupNeeded());
  schedule.Start(RecoveryPass::kStorage);Check(schedule.Commit());Check(schedule.Finish());
  Check(schedule.DemandCleanupNeeded());Check(rejects([&]{schedule.EnterReadValues();}));
  schedule.Start(RecoveryPass::kSnapshots);Check(schedule.Commit());Check(schedule.Finish());
  Check(schedule.DemandCleanupNeeded());
  Check(rejects([&]{schedule.EnterGrammar();}));cleanup(schedule);
  schedule.EnterReadValues();Check(rejects([&]{schedule.EnterGrammar();}));
  for(const auto pass:{RecoveryPass::kAliases,RecoveryPass::kStorage,RecoveryPass::kSnapshots})Check(rejects([&]{schedule.Start(pass);}));
  schedule.Start(RecoveryPass::kBindingValues);schedule.RequireFact(RecoveryFact::kConstants);schedule.RequireFact(RecoveryFact::kLiveness);
  schedule.RequireFact(RecoveryFact::kCopies);
  Check(schedule.Commit());Check(schedule.Finish());Check(rejects([&]{schedule.EnterGrammar();}));cleanup(schedule);
  schedule.EnterGrammar();
  Check(rejects([&]{schedule.Start(RecoveryPass::kAliases);}));
  Check(rejects([&]{schedule.EnterStorage();}));
  schedule.Start(RecoveryPass::kLoopDeclarations);
  Check(rejects([&]{schedule.RequireFact(RecoveryFact::kFlow);}));
  Check(!schedule.Finish());
  schedule.Start(RecoveryPass::kDeclarationKinds);schedule.RequireFact(RecoveryFact::kFlow);
  Check(!schedule.Finish());
  for(unsigned count:{200U,201U,400U,800U}) {
    RecoverySchedule repeated;cleanup(repeated);
    for(unsigned i=0;i<count;++i) {
      repeated.Start(RecoveryPass::kStoredResults);
      Check(!repeated.Finish());Check(!repeated.DemandCleanupNeeded());
      repeated.Start(RecoveryPass::kInertOperands);Check(repeated.Commit());
      Check(repeated.Finish());Check(repeated.DemandCleanupNeeded());cleanup(repeated);
    }
    Check(repeated.Revision().value==count);
  }
  RecoverySchedule unfinished;
  Check(rejects([&]{unfinished.Start(RecoveryPass::kDeclarations);}));
  Check(rejects([&]{unfinished.Start(RecoveryPass::kInitialValues);}));
  Check(rejects([&]{unfinished.Start(RecoveryPass::kBindings);}));
  unfinished.Start(RecoveryPass::kDeadDefinitions);Check(!unfinished.Finish());
  Check(rejects([&]{unfinished.Start(RecoveryPass::kBindings);}));
  Check(rejects([&]{unfinished.Start(RecoveryPass::kConstants);}));
  Check(rejects([&]{unfinished.EnterStorage();}));
  unfinished.Start(RecoveryPass::kInitialValues);Check(!unfinished.Finish());
  unfinished.Start(RecoveryPass::kBindings);Check(!unfinished.Finish());
  Check(!unfinished.DemandCleanupNeeded());
  unfinished.Start(RecoveryPass::kDeclarations);unfinished.RequireFact(RecoveryFact::kFlow);
  Check(unfinished.Commit());Check(unfinished.Finish());Check(unfinished.DemandCleanupNeeded());
  Check(rejects([&]{unfinished.Start(RecoveryPass::kAliases);}));
  Check(rejects([&]{unfinished.EnterStorage();}));cleanup(unfinished);
  unfinished.Start(RecoveryPass::kDeclarations);Check(!unfinished.Finish());
  Check(!unfinished.DemandCleanupNeeded());
  std::cout<<"pass ownership, dependency and cleanup counterexamples passed\n";
}
int main(){InitializedRepresentatives();GroupRepresentatives();ReadConstants();StorageScopePreference();AvailableCopyOracle();TransitiveCopyOracle();InitializationTransfers();PassContracts();SnapshotCheckpoints();StableValues();ExpressionConstants();ConstantEquations();SourceScopeOracle();BindingNameOracle();SourceNameOracle();RetirementOracle();SpellingOracle();StorageNameOracle();StoragePrerequisitePreference();StorageInitializationSubsets();StorageNameHoles();InitialValueObligations();DefinitionSites();DeclarationValues();GroupOracle();RefinedInitializations();UniversalObservations();CompletionDominance();PreservedInitializations();SourceEvents();SmallGraphs();DemandGraphs();Scaling();}
