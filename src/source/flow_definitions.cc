#include "flow_definitions.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowReadCoverage,TreeStructureError> AnalyzeFlowReadCoverage(
    const RecoveryFlow& flow,const FlowLiveness& live,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowReadCoverage result;
    std::vector<std::optional<FlowDefinitionId>> observed;
    std::vector<std::uint8_t> conflicting;
    budget.Array(observed,flow.bindings.size());budget.Array(conflicting,flow.bindings.size());
    budget.Array(result.definitions,flow.definitions.size());
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();if(!live.live_definitions[i])continue;
      const auto binding=flow.definitions[i].binding;
      if(observed[binding.value])conflicting[binding.value]=1;
      else observed[binding.value]=FlowDefinitionId{static_cast<std::uint32_t>(i)};
    }
    for(const auto& fact:live.live_in) {
      budget.Step();if(fact.point==flow.entry||flow.ResetsBinding(fact.binding,fact.point))conflicting[fact.binding.value]=1;
    }
    for(const auto& fact:live.unavailable){budget.Step();conflicting[fact.binding.value]=1;}
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto binding=flow.definitions[i].binding;
      result.definitions[i]=!conflicting[binding.value] &&
          (!observed[binding.value] || observed[binding.value]->value==i);
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
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
        budget.Step();if(flow.TransferBinding(definition.binding,flow.edges[*edge])!=FlowUnitTransfer::kEnded)
          add({flow.edges[*edge].to,{static_cast<std::uint32_t>(i)}});
      }
    }
    while(!pending.empty()) {
      budget.Step();const auto fact=pending.back();pending.pop_back();
      const auto binding=flow.definitions[fact.definition.value].binding;
      const auto& event=flow.events[fact.point.value];
      if(event.read==binding)reads.insert(fact);
      if(event.definition&&flow.definitions[event.definition->value].binding==binding)continue;
      for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();if(flow.TransferBinding(binding,flow.edges[*edge])!=FlowUnitTransfer::kEnded)
          add({flow.edges[*edge].to,fact.definition});
      }
    }
    FlowDefinitions result;budget.Array(result.reads,reads.size());
    std::copy(reads.begin(),reads.end(),result.reads.begin());
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
