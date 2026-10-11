#include "flow_cycles.h"
#include "tree_checks.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowCycles,TreeStructureError> AnalyzeFlowCycles(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowCycles result;const auto size=flow.events.size();
    RequireTree(size<std::numeric_limits<std::uint32_t>::max()&&flow.edges.size()<std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    std::vector<std::uint32_t> outgoing,incoming;std::vector<FlowPoint> next,previous,finished,component;
    std::vector<std::uint8_t> seen;
    budget.Array(result.cyclic,size);budget.Array(result.components,size);budget.Array(seen,size);
    budget.Array(outgoing,size+1);budget.Array(incoming,size+1);
    budget.Array(next,flow.edges.size());budget.Array(previous,flow.edges.size());
    budget.Array(finished,size);budget.Array(component,size);
    for(const auto& edge:flow.edges) {
      budget.Step();RequireTree(edge.from.value<size&&edge.to.value<size,TreeStructureError::kTarget);
      ++outgoing[edge.from.value+1];++incoming[edge.to.value+1];
    }
    for(std::size_t i=1;i<=size;++i){budget.Step();outgoing[i]+=outgoing[i-1];incoming[i]+=incoming[i-1];}
    // Fill with the offsets as cursors, then restore starts by a right shift.
    // The edge vector is authoritative; no linked-index layout is assumed.
    for(const auto& edge:flow.edges){budget.Step();next[outgoing[edge.from.value]++]=edge.to;previous[incoming[edge.to.value]++]=edge.from;}
    for(std::size_t i=size;i;--i){budget.Step();outgoing[i]=outgoing[i-1];incoming[i]=incoming[i-1];}
    outgoing[0]=incoming[0]=0;
    struct Frame {FlowPoint point;std::uint32_t next;};std::vector<Frame> stack;budget.Array(stack,size);
    std::size_t count=0;
    for(std::size_t root=0;root<size;++root) {
      budget.Step();if(seen[root])continue;std::size_t depth=1;
      seen[root]=1;stack[0]={{static_cast<std::uint32_t>(root)},outgoing[root]};
      while(depth) {
        budget.Step();auto& frame=stack[depth-1];
        if(frame.next==outgoing[frame.point.value+1]){finished[count++]=frame.point;--depth;continue;}
        const auto target=next[frame.next++];if(seen[target.value])continue;
        seen[target.value]=1;stack[depth++]={target,outgoing[target.value]};
      }
    }
    std::fill(seen.begin(),seen.end(),0);budget.Step(size);
    std::uint32_t order=0;
    while(count) {
      budget.Step();const auto root=finished[--count];if(seen[root.value])continue;
      std::size_t head=0,members=1;component[0]=root;seen[root.value]=1;
      while(head<members) {
        budget.Step();const auto point=component[head++];
        for(auto at=incoming[point.value];at<incoming[point.value+1];++at) {
          budget.Step();const auto target=previous[at];if(seen[target.value])continue;
          seen[target.value]=1;component[members++]=target;
        }
      }
      bool cyclic=members>1;
      if(!cyclic)for(auto at=outgoing[root.value];at<outgoing[root.value+1];++at){budget.Step();cyclic|=next[at]==root;}
      for(std::size_t i=0;i<members;++i){budget.Step();result.cyclic[component[i].value]=cyclic;result.components[component[i].value]={order};}
      ++order;
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;
  }catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;
  }catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
