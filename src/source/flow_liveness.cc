#include "flow_liveness.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowLiveness,TreeStructureError> AnalyzeFlowLiveness(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);FlowLiveness result;
    budget.Array(result.live_definitions,flow.definitions.size());
    const auto less=[&](const LiveBinding& a,const LiveBinding& b){budget.Step();return a<b;};
    std::pmr::set<LiveBinding,decltype(less)> facts(less,&memory);
    std::pmr::vector<LiveBinding> pending(&memory);
    const auto add=[&](LiveBinding fact){if(facts.insert(fact).second)pending.push_back(fact);};
    for(std::size_t i=0;i<flow.events.size();++i){budget.Step();
      if(flow.events[i].read)add({{static_cast<std::uint32_t>(i)},*flow.events[i].read});}
    while(!pending.empty()) {
      const auto fact=pending.back();pending.pop_back();budget.Step();
      for(auto edge=flow.events[fact.point.value].incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto predecessor=flow.edges[*edge].from;
        const auto definition=flow.events[predecessor.value].definition;
        if(definition&&flow.definitions[definition->value].binding==fact.binding)
          result.live_definitions[definition->value]=1;
        else add({predecessor,fact.binding});
      }
    }
    budget.Array(result.live_in,facts.size());std::copy(facts.begin(),facts.end(),result.live_in.begin());
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
