#include "flow_constants.h"
#include "primitive_value.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow& flow,const FlowLiveness& live,std::span<const FlowDefinitionValue> values,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);
    RequireTree(values.size()==flow.definitions.size()&&live.live_definitions.size()==values.size(),TreeStructureError::kOwnership);
    struct DependencyId {std::uint32_t value;};
    struct Edge {SymbolId source,target;std::optional<DependencyId> outgoing,incoming;};
    struct ComponentId {std::uint32_t value;};
    std::vector<Edge> edges;std::vector<std::optional<DependencyId>> heads,reverse,cursor;
    std::vector<std::uint8_t> seen;std::vector<SymbolId> stack,finished;
    std::vector<std::optional<ComponentId>> components;
    const auto size=flow.bindings.size();
    RequireTree(size<=std::numeric_limits<std::uint32_t>::max()&&values.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
    const auto retained=[&](std::size_t i){return flow.definitions[i].kind!=FlowDefinitionKind::kUndefined||live.live_definitions[i];};
    std::size_t copies=0;
    for(std::size_t i=0;i<values.size();++i){budget.Step();copies+=retained(i)&&std::holds_alternative<FlowBindingValue>(values[i]);}
    // Match the common flow's strong 32-bit identity domain. Allocate sparse
    // copy edges, not slots for primitive/unknown definitions; no fact is lost.
    budget.Array(edges,copies);budget.Array(heads,size);budget.Array(reverse,size);budget.Array(cursor,size);
    budget.Array(seen,size);budget.Array(stack,size);budget.Array(finished,size);budget.Array(components,size);
    std::size_t count=0;
    for(std::size_t i=0;i<values.size();++i) {
      budget.Step();if(!retained(i))continue;
      if(const auto* copy=std::get_if<FlowBindingValue>(&values[i])) {
        const auto target=flow.definitions[i].binding;const auto source=copy->binding;
        RequireTree(source.value<size&&target.value<size,TreeStructureError::kOwnership);
        const DependencyId edge{static_cast<std::uint32_t>(count++)};
        edges[edge.value]={source,target,heads[source.value],reverse[target.value]};heads[source.value]=edge;reverse[target.value]=edge;
      }
    }
    edges.resize(count);std::copy(heads.begin(),heads.end(),cursor.begin());
    // Iterative Kosaraju, as in parameter_origins: one sparse dependency graph,
    // then its reverse. Component IDs follow source-before-dependent order.
    std::size_t pending=0,finish=0;
    for(std::size_t root=0;root<size;++root) {
      budget.Step();if(seen[root])continue;seen[root]=1;stack[pending++]={static_cast<std::uint32_t>(root)};
      while(pending) {
        budget.Step();const auto at=stack[pending-1];
        if(const auto edge=cursor[at.value]) {
          cursor[at.value]=edges[edge->value].outgoing;const auto next=edges[edge->value].target;
          if(!seen[next.value]){seen[next.value]=1;stack[pending++]=next;}
        }else {finished[finish++]=at;--pending;}
      }
    }
    std::size_t component_count=0;
    while(finish) {
      budget.Step();const auto root=finished[--finish];if(components[root.value])continue;
      const ComponentId component{static_cast<std::uint32_t>(component_count++)};components[root.value]=component;stack[pending++]=root;
      while(pending) {
        budget.Step();const auto at=stack[--pending];
        for(auto edge=reverse[at.value];edge;edge=edges[edge->value].incoming) {
          budget.Step();const auto next=edges[edge->value].source;
          if(!components[next.value]){components[next.value]=component;stack[pending++]=next;}
        }
      }
    }
    std::vector<std::optional<FlowPrimitiveValue>> observed;
    std::vector<std::uint8_t> blocked;std::vector<std::optional<DependencyId>> outgoing,next;
    budget.Array(observed,component_count);budget.Array(blocked,component_count);
    budget.Array(outgoing,component_count);budget.Array(next,edges.size());
    const auto merge=[&](ComponentId component,const FlowPrimitiveValue& value) {
      auto& current=observed[component.value];
      if(!current)current=value;
      else if(!SamePrimitiveValue(current->value,value.value))blocked[component.value]=1;
      else if(!current->literal)current->literal=value.literal;
    };
    for(std::size_t i=0;i<size;++i){budget.Step();if(flow.bindings[i].exposure!=FlowBindingExposure::kPrivate)blocked[components[i]->value]=1;}
    for(const auto& fact:live.live_in){budget.Step();if(fact.point==flow.entry)blocked[components[fact.binding.value]->value]=1;}
    for(std::size_t i=0;i<values.size();++i) {
      budget.Step();if(!retained(i))continue;const auto component=*components[flow.definitions[i].binding.value];
      std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,FlowPrimitiveValue>)merge(component,value);
        else if constexpr(std::is_same_v<T,UnknownFlowValue>)blocked[component.value]=1;
        else static_assert(std::is_same_v<T,FlowBindingValue>);
      },values[i]);
    }
    for(std::size_t i=0;i<edges.size();++i) {
      budget.Step();const auto a=*components[edges[i].source.value],b=*components[edges[i].target.value];
      if(a.value==b.value)continue;
      RequireTree(a.value<b.value,TreeStructureError::kOwnership);
      next[i]=outgoing[a.value];outgoing[a.value]=DependencyId{static_cast<std::uint32_t>(i)};
    }
    for(std::size_t i=0;i<component_count;++i) {
      budget.Step();for(auto edge=outgoing[i];edge;edge=next[edge->value]) {
        budget.Step();const auto target=*components[edges[edge->value].target.value];
        if(blocked[i]||!observed[i])blocked[target.value]=1;else merge(target,*observed[i]);
      }
    }
    FlowConstants result;budget.Array(result.values,size);
    for(std::size_t i=0;i<size;++i){budget.Step();const auto component=components[i]->value;if(!blocked[component])result.values[i]=observed[component];}
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
