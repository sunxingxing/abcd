#include "flow_definitions.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowDefinitions,TreeStructureError> AnalyzeFlowDefinitions(
    const RecoveryFlow& flow,const FlowLiveness& live,analysis::SemanticBudget allowance) {
  using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);
    const auto less=[&](const ReachingDefinition& a,const ReachingDefinition& b){budget.Step();return a<b;};
    std::pmr::set<ReachingDefinition,decltype(less)> facts(less,&memory),reads(less,&memory);
    std::pmr::vector<ReachingDefinition> pending(&memory);
    const auto add=[&](ReachingDefinition fact) {
      const LiveBinding key{fact.point,flow.definitions[fact.definition.value].binding};
      const auto found=std::lower_bound(live.live_in.begin(),live.live_in.end(),key,
          [&](auto a,auto b){budget.Step();return a<b;});
      if(found!=live.live_in.end()&&*found==key&&facts.insert(fact).second)pending.push_back(fact);
    };
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();if(!live.live_definitions[i])continue;
      const auto& definition=flow.definitions[i];
      for(auto edge=flow.events[definition.point.value].outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();add({flow.edges[*edge].to,{static_cast<std::uint32_t>(i)}});
      }
    }
    while(!pending.empty()) {
      budget.Step();const auto fact=pending.back();pending.pop_back();
      const auto binding=flow.definitions[fact.definition.value].binding;
      const auto& event=flow.events[fact.point.value];
      if(event.read==binding)reads.insert(fact);
      if(event.definition&&flow.definitions[event.definition->value].binding==binding)continue;
      for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();add({flow.edges[*edge].to,fact.definition});
      }
    }
    FlowDefinitions result;budget.Array(result.reads,reads.size());
    std::copy(reads.begin(),reads.end(),result.reads.begin());
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
