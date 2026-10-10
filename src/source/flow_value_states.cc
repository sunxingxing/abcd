#include "flow_value_states.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowValueStates,TreeStructureError> BuildFlowValueStates(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  using Error=TreeStructureError;
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);
    FlowValueStates result;result.owner=&flow;
    const auto count=flow.events.size();
    RequireTree(count<=std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    budget.Array(result.reachable,count);
    std::pmr::vector<FlowPoint> pending(&memory);
    if(count){RequireTree(flow.entry.value<count,Error::kTarget);pending.push_back(flow.entry);result.reachable[flow.entry.value]=1;}
    while(!pending.empty()) {
      budget.Step();const auto point=pending.back();pending.pop_back();
      for(auto edge=flow.events[point.value].outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();const auto target=flow.edges[*edge].to;RequireTree(target.value<count,Error::kTarget);
        if(!result.reachable[target.value]){result.reachable[target.value]=1;pending.push_back(target);}
      }
    }
    // Single-predecessor regions form a forest even when a node branches.
    // This is sparse evaluation graph construction, never a recovery admission:
    // every other edge is retained at an explicit join/unit boundary.
    const auto boundary=[&](FlowPoint point) {
      const auto& event=flow.events[point.value];
      return event.ResetsScopeUnit()||event.ResetsLoopUnit();
    };
    const auto same_context=[&](const FlowEdge& edge) {
      if(boundary(edge.from)||boundary(edge.to))return false;
      if(!flow.scopes)return true;
      const auto& scopes=*flow.scopes;
      const auto unchanged=[&](const auto& contexts) {
        // An absent context table stays absent; TransferBinding still rejects
        // units requiring it. Partial tables cannot certify interval equality.
        if(contexts.empty())return true;
        if(contexts.size()!=count)return false;
        const auto& from=contexts[edge.from.value];const auto& to=contexts[edge.to.value];
        return from.before==from.after&&from.after==to.before&&to.before==to.after;
      };
      return unchanged(scopes.events)&&unchanged(scopes.loop_events);
    };
    std::pmr::vector<std::optional<std::size_t>> parents(&memory);
    parents.resize(count);budget.Step(count);
    for(std::size_t i=0;i<count;++i) {
      budget.Step();if(!result.reachable[i])continue;
      std::size_t degree=0;std::optional<std::size_t> only;
      for(auto edge=flow.events[i].incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();if(result.reachable[flow.edges[*edge].from.value]){++degree;only=*edge;}
      }
      if(FlowPoint{static_cast<std::uint32_t>(i)}!=flow.entry&&degree==1&&same_context(flow.edges[*only]))parents[i]=only;
      else pending.push_back({static_cast<std::uint32_t>(i)});
    }
    struct Action {FlowPoint point;SymbolId binding;auto operator<=>(const Action&)const=default;};
    std::pmr::vector<Action> actions(&memory);
    for(const auto& definition:flow.definitions){budget.Step();actions.push_back({definition.point,definition.binding});}
    for(const auto& reference:flow.references){budget.Step();if(reference.kind==FlowReferenceKind::kWrite)actions.push_back({reference.point,reference.binding});}
    for(std::size_t i=0;i<flow.bindings.size();++i){budget.Step();if(flow.bindings[i].allocation)actions.push_back({*flow.bindings[i].allocation,SymbolId{static_cast<std::uint32_t>(i)}});}
    const auto action_less=[&](const Action& a,const Action& b){budget.Step();return a<b;};
    std::sort(actions.begin(),actions.end(),action_less);
    actions.erase(std::unique(actions.begin(),actions.end()),actions.end());
    // Path-copying array: O(log B) per actual binding action, O(log B) per
    // query. No B-element environment copy and no graph walk per binding.
    struct MapNode {std::optional<std::uint32_t> left,right;FlowPoint value;};
    std::pmr::vector<MapNode> maps(&memory);
    struct State {std::optional<std::uint32_t> map;FlowPoint root;std::optional<FlowPoint> callback;std::uint32_t depth=0;};
    std::pmr::vector<State> states(&memory);states.resize(count);budget.Step(count);
    const auto set=[&](auto&& self,std::optional<std::uint32_t> prior,std::size_t first,std::size_t last,SymbolId binding,FlowPoint value)->std::uint32_t {
      budget.Step();RequireTree(first<last&&binding.value>=first&&binding.value<last,Error::kBinding);
      RequireTree(maps.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
      const auto id=static_cast<std::uint32_t>(maps.size());maps.push_back(prior?maps[*prior]:MapNode{});
      if(last-first==1){maps[id].value=value;return id;}
      const auto middle=first+(last-first)/2;
      if(binding.value<middle){const auto child=self(self,maps[id].left,first,middle,binding,value);maps[id].left=child;}
      else {const auto child=self(self,maps[id].right,middle,last,binding,value);maps[id].right=child;}
      return id;
    };
    std::size_t visited=0;
    while(!pending.empty()) {
      budget.Step();const auto point=pending.back();pending.pop_back();++visited;
      auto& state=states[point.value];
      if(const auto parent=parents[point.value]) {
        const auto& edge=flow.edges[*parent];state=states[edge.from.value];++state.depth;
        auto action=std::lower_bound(actions.begin(),actions.end(),Action{edge.from,SymbolId{}},action_less);
        for(;action!=actions.end()&&action->point==edge.from;++action){budget.Step();state.map=set(set,state.map,0,flow.bindings.size(),action->binding,point);}
        if(flow.events[edge.from.value].MayInvokeUserCode(edge.completion))state.callback=point;
      } else state.root=point;
      for(auto edge=flow.events[point.value].outgoing;edge;edge=flow.edges[*edge].outgoing) {
        budget.Step();const auto target=flow.edges[*edge].to;if(parents[target.value]==edge)pending.push_back(target);
      }
    }
    std::size_t reached=0;for(const auto value:result.reachable){budget.Step();reached+=value!=0;}
    RequireTree(visited==reached,Error::kOwnership);
    const auto canonical=[&](LiveBinding fact) {
      RequireTree(fact.point.value<count&&fact.binding.value<flow.bindings.size(),Error::kOwnership);
      if(!result.reachable[fact.point.value])return fact;
      const auto& state=states[fact.point.value];auto point=state.root;auto node=state.map;
      std::size_t first=0,last=flow.bindings.size();
      while(node) {
        budget.Step();if(last-first==1){point=maps[*node].value;break;}
        const auto middle=first+(last-first)/2;
        if(fact.binding.value<middle){node=maps[*node].left;last=middle;}
        else {node=maps[*node].right;first=middle;}
      }
      if(flow.bindings[fact.binding.value].MayBeChangedByUserCode()&&state.callback&&
          states[point.value].depth<states[state.callback->value].depth)point=*state.callback;
      return LiveBinding{point,fact.binding};
    };
    const auto less=[&](const LiveBinding& a,const LiveBinding& b){budget.Step();return a<b;};
    std::pmr::set<LiveBinding,decltype(less)> facts(less,&memory);
    std::pmr::vector<LiveBinding> queries(&memory);
    const auto add=[&](LiveBinding fact){if(facts.insert(fact).second)queries.push_back(fact);};
    for(const auto& reference:flow.references){budget.Step();if(reference.kind==FlowReferenceKind::kRead)add({reference.point,reference.binding});}
    for(std::size_t i=0;i<count;++i){budget.Step();if(flow.events[i].read)add({FlowPoint{static_cast<std::uint32_t>(i)},*flow.events[i].read});}
    for(const auto& definition:flow.definitions){budget.Step();if(definition.input)add({definition.input->point,definition.input->binding});}
    for(const auto& input:flow.captured_inputs) {
      budget.Step();RequireTree(input.value.point.value<count&&input.value.binding.value<flow.bindings.size(),Error::kOwnership);
      add({input.value.point,input.value.binding});
    }
    while(!queries.empty()) {
      budget.Step();const auto fact=queries.back();queries.pop_back();
      if(!result.reachable[fact.point.value])continue;
      const auto source=canonical(fact);if(source!=fact){add(source);continue;}
      for(auto edge=flow.events[fact.point.value].incoming;edge;edge=flow.edges[*edge].incoming) {
        budget.Step();const auto& incoming=flow.edges[*edge];
        if(!result.reachable[incoming.from.value]||flow.TransferBinding(fact.binding,incoming)!=FlowUnitTransfer::kPreserved)continue;
        const auto definition=flow.events[incoming.from.value].definition;
        if(definition&&flow.definitions[definition->value].binding==fact.binding)continue;
        // Allocation actions are value boundaries too, but only the common
        // transfer above decides whether the unit survives an allocation.
        const auto action=std::lower_bound(actions.begin(),actions.end(),Action{incoming.from,fact.binding},action_less);
        if(action!=actions.end()&&action->point==incoming.from&&action->binding==fact.binding)continue;
        add({incoming.from,fact.binding});
      }
    }
    budget.Array(result.states,facts.size());std::copy(facts.begin(),facts.end(),result.states.begin());
    budget.Array(result.forwarded,facts.size());
    for(std::size_t i=0;i<result.states.size();++i){budget.Step();const auto source=canonical(result.states[i]);if(source!=result.states[i])result.forwarded[i]=source;}
    result.retained_bytes=result.states.capacity()*sizeof(LiveBinding)+result.forwarded.capacity()*sizeof(std::optional<LiveBinding>)+result.reachable.capacity();
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
