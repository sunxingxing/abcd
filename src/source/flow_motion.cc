#include "flow_motion.h"
#include "tree_checks.h"
#include <stdexcept>
#include <limits>
namespace abcd::source {
namespace { enum class Direction { kForward,kBackward };
std::variant<FlowMotion,TreeStructureError> SolveMotion(
    const RecoveryFlow& flow,std::span<const std::uint8_t> invocation,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowMotion result;
    const auto size=flow.events.size();
    RequireTree(size<std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    std::vector<std::size_t> remaining;
    std::vector<std::optional<FlowPoint>> first;
    std::vector<std::uint8_t> anchors,ready,joined,reachable;
    std::vector<FlowPoint> queue;
    budget.Array(remaining,size);budget.Array(first,size);budget.Array(anchors,size);
    budget.Array(ready,size);budget.Array(joined,size);budget.Array(queue,size);
    if(invocation.empty())budget.Array(reachable,size);
    std::size_t count=0;
    if(size&&invocation.empty()) {
      RequireTree(flow.entry.value<size,TreeStructureError::kTarget);
      queue[count++]=flow.entry;reachable[flow.entry.value]=1;
    }
    for(std::size_t at=0;at<count;++at) {
      budget.Step();
      for(auto edge=flow.events[queue[at].value].outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();const auto target=flow.edges[*edge].to;
        if(!reachable[target.value]){reachable[target.value]=1;queue[count++]=target;}
      }
    }
    if(!invocation.empty())RequireTree(invocation.size()==size,TreeStructureError::kOwnership);
    const auto reached=invocation.empty()?std::span<const std::uint8_t>{reachable}:invocation;
    const auto solve=[&](Direction direction,std::vector<std::optional<FlowPoint>>& output) {
      const bool forward=direction==Direction::kForward;
      const auto head=[&](std::size_t point,bool along){return along==forward?flow.events[point].outgoing:flow.events[point].incoming;};
      const auto next=[&](std::size_t edge,bool along){return along==forward?flow.edges[edge].outgoing:flow.edges[edge].incoming;};
      const auto target=[&](std::size_t edge,bool along){return along==forward?flow.edges[edge].to:flow.edges[edge].from;};
      const auto admitted=[&](FlowPoint point){return forward || reached[point.value];};
      count=0;
      for(std::size_t i=0;i<size;++i) {
        budget.Step();remaining[i]=0;first[i].reset();anchors[i]=ready[i]=joined[i]=0;
        if(!forward && !reached[i])continue;
        const auto& event=flow.events[i];
        for(auto edge=head(i,true);edge;edge=next(*edge,true)){budget.Step();if(admitted(target(*edge,true)))++remaining[i];}
        // Backward invocation history stops at entry. Unreachable structural
        // exits must not create fictitious earlier activations of a real read.
        anchors[i]=event.read || event.definition || event.effect==ExpressionEvaluation::kRequired ||
            !remaining[i] || (!forward && i==flow.entry.value);
        if(anchors[i]){first[i]=FlowPoint{static_cast<std::uint32_t>(i)};ready[i]=1;queue[count++]=*first[i];}
      }
      for(std::size_t at=0;at<count;++at) {
        budget.Step();const auto point=queue[at];
        for(auto edge=head(point.value,false);edge;edge=next(*edge,false)) {
          budget.Step();const auto from=target(*edge,false);
          if(!admitted(from) || anchors[from.value])continue;
          if(!joined[from.value]){first[from.value]=first[point.value];joined[from.value]=1;}
          else if(first[from.value]!=first[point.value])first[from.value].reset();
          RequireTree(remaining[from.value]!=0,TreeStructureError::kOwnership);
          if(--remaining[from.value]==0) {
            ready[from.value]=1;RequireTree(count<queue.size(),TreeStructureError::kOwnership);queue[count++]=from;
          }
        }
      }
      budget.Array(output,size);
      for(std::size_t i=0;i<size;++i) {
        budget.Step();if(!forward && !reached[i])continue;
        bool seen=false,agrees=true;
        for(auto edge=head(i,true);edge;edge=next(*edge,true)) {
          budget.Step();const auto to=target(*edge,true);if(!admitted(to))continue;
          if(!ready[to.value] || !first[to.value])agrees=false;
          if(!seen){output[i]=first[to.value];seen=true;}
          else if(output[i]!=first[to.value])agrees=false;
        }
        if(!agrees)output[i].reset();
      }
    };
    solve(Direction::kForward,result.next);solve(Direction::kBackward,result.previous);
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
} // namespace
std::variant<FlowMotion,TreeStructureError> AnalyzeFlowMotion(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  return SolveMotion(flow,{},allowance);
}
std::variant<FlowMotion,TreeStructureError> AnalyzeFlowMotion(
    const RecoveryFlow& flow,const FlowLiveness& live,analysis::SemanticBudget allowance) {
  if(live.reachable.size()!=flow.events.size())return TreeStructureError::kOwnership;
  return SolveMotion(flow,live.reachable,allowance);
}
}
