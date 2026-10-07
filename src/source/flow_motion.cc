#include "flow_motion.h"
#include "tree_checks.h"
#include <stdexcept>
#include <limits>
namespace abcd::source {
std::variant<FlowMotion,TreeStructureError> AnalyzeFlowMotion(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowMotion result;
    const auto size=flow.events.size();
    RequireTree(size<std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    std::vector<std::size_t> remaining;
    std::vector<std::optional<FlowPoint>> first;
    std::vector<std::uint8_t> anchors,ready,joined;
    std::vector<FlowPoint> queue;
    budget.Array(remaining,size);budget.Array(first,size);budget.Array(anchors,size);
    budget.Array(ready,size);budget.Array(joined,size);budget.Array(queue,size);
    std::size_t count=0;
    for(std::size_t i=0;i<size;++i) {
      budget.Step();const auto& event=flow.events[i];
      for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing){budget.Step();++remaining[i];}
      anchors[i]=event.read || event.definition || event.effect==ExpressionEvaluation::kRequired || !remaining[i];
      if(anchors[i]) {first[i]=FlowPoint{static_cast<std::uint32_t>(i)};ready[i]=1;queue[count++]=*first[i];}
    }
    for(std::size_t at=0;at<count;++at) {
      budget.Step();const auto point=queue[at];
      for(auto edge=flow.events[point.value].incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto from=flow.edges[*edge].from;
        if(anchors[from.value])continue;
        // Every outgoing edge contributes once, when its successor resolves.
        // A resolved conflict carries absence; it must still release parents.
        if(!joined[from.value]){first[from.value]=first[point.value];joined[from.value]=1;}
        else if(first[from.value]!=first[point.value])first[from.value].reset();
        RequireTree(remaining[from.value]!=0,TreeStructureError::kOwnership);
        if(--remaining[from.value]==0) {
          ready[from.value]=1;RequireTree(count<queue.size(),TreeStructureError::kOwnership);
          queue[count++]=from;
        }
      }
    }
    budget.Array(result.next,size);
    for(std::size_t i=0;i<size;++i) {
      budget.Step();bool seen=false,agrees=true;
      for(auto edge=flow.events[i].outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();const auto target=flow.edges[*edge].to;
        if(!ready[target.value] || !first[target.value])agrees=false;
        if(!seen){result.next[i]=first[target.value];seen=true;}
        else if(result.next[i]!=first[target.value])agrees=false;
      }
      if(!agrees)result.next[i].reset();
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
