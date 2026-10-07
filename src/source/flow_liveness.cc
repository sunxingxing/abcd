#include "flow_liveness.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace abcd::source {
namespace {
enum class Roots { kAllReads, kObservable };
struct LivenessResult { FlowLiveness liveness; std::vector<std::uint8_t> retained; };
std::variant<LivenessResult,TreeStructureError> Solve(
    const RecoveryFlow& flow,Roots roots,analysis::SemanticBudget allowance) {
  using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);LivenessResult result;
    budget.Array(result.liveness.live_definitions,flow.definitions.size());
    const auto less=[&](const LiveBinding& a,const LiveBinding& b){budget.Step();return a<b;};
    std::pmr::set<LiveBinding,decltype(less)> facts(less,&memory);
    std::pmr::vector<LiveBinding> pending(&memory);
    std::pmr::vector<std::optional<FlowPoint>> heads(&memory),next(&memory);
    if(roots==Roots::kObservable) {
      budget.Array(result.retained,flow.discarded.size());
      budget.Step(flow.discarded.size());heads.resize(flow.discarded.size());
      budget.Step(flow.events.size());next.resize(flow.events.size());
    }
    const auto add=[&](LiveBinding fact){if(facts.insert(fact).second)pending.push_back(fact);};
    const auto retain=[&](DiscardedId id) {
      if(result.retained[id.value])return;
      result.retained[id.value]=1;
      for(auto point=heads[id.value];point;point=next[point->value]) {
        budget.Step();add({*point,*flow.events[point->value].read});
      }
    };
    for(std::size_t i=0;i<flow.events.size();++i) {
      budget.Step();const auto& event=flow.events[i];if(!event.read)continue;
      const FlowPoint point{static_cast<std::uint32_t>(i)};
      if(roots==Roots::kObservable && event.discarded) {
        next[i]=heads[event.discarded->value];heads[event.discarded->value]=point;
      } else add({point,*event.read});
    }
    if(roots==Roots::kObservable)for(std::size_t i=0;i<flow.discarded.size();++i) {
      budget.Step();if(flow.discarded[i].effect==ExpressionEvaluation::kRequired)retain({static_cast<std::uint32_t>(i)});
    }
    // Both analyses share gen/kill and exception-edge traversal. Observable
    // roots discover def-use dependencies on demand, without materializing a
    // potentially dense read/definition product. Regions activate only once.
    while(!pending.empty()) {
      const auto fact=pending.back();pending.pop_back();budget.Step();
      for(auto edge=flow.events[fact.point.value].incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto predecessor=flow.edges[*edge].from;
        const auto& event=flow.events[predecessor.value];const auto definition=event.definition;
        if(definition&&flow.definitions[definition->value].binding==fact.binding) {
          if(!result.liveness.live_definitions[definition->value]) {
            result.liveness.live_definitions[definition->value]=1;
            if(roots==Roots::kObservable && event.discarded)retain(*event.discarded);
          }
        } else add({predecessor,fact.binding});
      }
    }
    if(roots==Roots::kAllReads) {
      budget.Array(result.liveness.live_in,facts.size());std::copy(facts.begin(),facts.end(),result.liveness.live_in.begin());
    }
    result.liveness.allocated_bytes=budget.Bytes();result.liveness.work=budget.Work();return result;
  } catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
std::variant<FlowLiveness,TreeStructureError> AnalyzeFlowLiveness(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  auto solved=Solve(flow,Roots::kAllReads,allowance);
  if(const auto* error=std::get_if<TreeStructureError>(&solved))return *error;
  return std::move(std::get<LivenessResult>(solved).liveness);
}
std::variant<FlowDemand,TreeStructureError> AnalyzeFlowDemand(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  auto solved=Solve(flow,Roots::kObservable,allowance);
  if(const auto* error=std::get_if<TreeStructureError>(&solved))return *error;
  auto facts=std::get<LivenessResult>(std::move(solved));
  return FlowDemand{std::move(facts.liveness.live_definitions),std::move(facts.retained),
      facts.liveness.allocated_bytes,facts.liveness.work};
}
}
