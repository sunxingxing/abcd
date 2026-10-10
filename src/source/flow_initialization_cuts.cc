#include "flow_initialization_cuts.h"
#include "tree_checks.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
std::variant<FlowInitializationCertificates,TreeStructureError> FindFlowInitializationCertificates(
    const RecoveryFlow& flow,const FlowDominance& order,
    std::span<const std::optional<FlowPoint>> anchors,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);RecoveryScratch memory(budget);
    RequireTree(anchors.size()==flow.bindings.size(),TreeStructureError::kOwnership);
    // A dominating gen certifies completion if its dominance subtree contains
    // no kill of THIS unit. Resets outside that subtree must revisit the gen
    // before any dominated query. Heavy-path differences index exactly the
    // units entered from inactive/unknown contexts; nested entry preserves the
    // parent unit. An offline Fenwick sweep answers all (unit, gen-subtree)
    // queries together. Uncertified states use the same full must equations.
    struct Kill {std::size_t first,last;std::uint32_t point;};
    struct Query {std::uint32_t point;std::size_t unit;SymbolId binding;bool upper;};
    std::pmr::vector<Kill> kills(&memory);std::pmr::vector<Query> certificates(&memory);
    FlowInitializationCertificates result;
    std::pmr::vector<std::int64_t> counts(&memory),fenwick(&memory);
    budget.Array(result.clear,flow.bindings.size());budget.Step(flow.bindings.size());counts.resize(flow.bindings.size());
    const auto scopes=flow.scopes.get();
    const auto physical=scopes?scopes->paths.size():0,iterations=scopes?scopes->loop_paths.size():0;
    RequireTree(physical<=std::numeric_limits<std::size_t>::max()-iterations-1,TreeStructureError::kResourceLimit);
    const auto units=physical+iterations;budget.Step(units+1);fenwick.resize(units+1);
    const auto kill=[&](std::size_t first,std::size_t last,FlowPoint point) {
      budget.Step();RequireTree(first<last&&last<=units,TreeStructureError::kScope);
      const auto position=order.tree.nodes[point.value].begin;if(position)kills.push_back({first,last,position});
    };
    const auto position=[](const auto& context,const auto& regions)->std::optional<ScopePosition> {
      return std::visit([&](const auto& value)->std::optional<ScopePosition>{using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ScopeId>||std::is_same_v<T,StmtId>) {
          if(value.value<regions.size()&&regions[value.value].first<regions[value.value].last)return regions[value.value].first;
        } else if constexpr(std::is_same_v<T,EnclosingScope>)
          if(value.scope.value<regions.size()&&regions[value.scope.value].first<regions[value.scope.value].last)return regions[value.scope.value].first;
        return {};},context);
    };
    const auto difference=[&](const auto& paths,std::optional<ScopePosition> before,
        std::optional<ScopePosition> after,std::size_t offset,FlowPoint point) {
      while(after) {
        budget.Step();RequireTree(after->value<paths.size(),TreeStructureError::kScope);
        const auto head=paths[after->value].head;
        if(before) {
          RequireTree(before->value<paths.size(),TreeStructureError::kScope);const auto prior=paths[before->value].head;
          if(prior==head) {if(before->value<after->value)kill(offset+before->value+1,offset+after->value+1,point);return;}
          if(paths[prior.value].depth>=paths[head.value].depth){before=paths[prior.value].parent;continue;}
        }
        kill(offset+head.value,offset+after->value+1,point);after=paths[head.value].parent;
      }
    };
    const auto physical_entry=[&](const FlowScopeContext& before,const FlowScopeContext& after,FlowPoint point) {
      if(!physical)return;
      if(std::holds_alternative<CompletionOnlyScope>(after)) {
        if(!std::holds_alternative<CompletionOnlyScope>(before))kill(0,physical,point);
      } else difference(scopes->paths,position(before,scopes->regions),position(after,scopes->regions),0,point);
    };
    const auto iteration_entry=[&](const FlowLoopContext& before,const FlowLoopContext& after,FlowPoint point) {
      const auto guaranteed=[&](const FlowLoopContext& context)->std::optional<ScopePosition> {
        if(const auto* exited=std::get_if<ExitedLoopScope>(&context)) {
          const auto at=position(FlowLoopContext{exited->loop},scopes->loop_regions);
          if(at&&at->value<iterations)return scopes->loop_paths[at->value].parent;return {};
        }
        return position(context,scopes->loop_regions);
      };
      if(iterations)difference(scopes->loop_paths,guaranteed(before),guaranteed(after),physical,point);
    };
    if(scopes) {
      for(std::size_t i=0;i<flow.events.size();++i) {
        budget.Step();const auto& event=flow.events[i];const FlowPoint point{static_cast<std::uint32_t>(i)};
        if(event.scope&&(event.kind==FlowEventKind::kEnterScope||
            (event.kind==FlowEventKind::kLeaveScope&&scopes->Presence(*event.scope,point,FlowScopePhase::kAfter)!=FlowUnitPresence::kInactive)))
          if(const auto at=position(FlowScopeContext{*event.scope},scopes->regions);at&&at->value<physical)kill(at->value,at->value+1,point);
        if(event.loop_environment&&(event.kind==FlowEventKind::kEnterLoopEnvironment||event.kind==FlowEventKind::kEnterIterationEnvironment))
          if(const auto at=position(FlowLoopContext{*event.loop_environment},scopes->loop_regions);at&&at->value<iterations)kill(physical+at->value,physical+at->value+1,point);
        if(i<scopes->events.size())physical_entry(scopes->events[i].before,scopes->events[i].after,point);
        if(i<scopes->loop_events.size())iteration_entry(scopes->loop_events[i].before,scopes->loop_events[i].after,point);
      }
      for(const auto& edge:flow.edges) {
        budget.Step();
        if(edge.to.value<scopes->events.size())physical_entry(edge.from.value<scopes->events.size()?scopes->events[edge.from.value].after:FlowScopeContext{UnmodeledScope{}},scopes->events[edge.to.value].before,edge.to);
        if(edge.to.value<scopes->loop_events.size())iteration_entry(edge.from.value<scopes->loop_events.size()?scopes->loop_events[edge.from.value].after:FlowLoopContext{UnmodeledScope{}},scopes->loop_events[edge.to.value].before,edge.to);
      }
    }
    for(std::size_t i=0;i<flow.bindings.size();++i) {
      budget.Step();const auto gen=anchors[i];if(!gen||!order.tree.nodes[gen->value].begin)continue;
      const auto& binding=flow.bindings[i];if(flow.BindingUnit(SymbolId{static_cast<std::uint32_t>(i)})==FlowUnitCapability::kUnmodeled)continue;
      if(binding.allocation&&binding.allocation!=gen&&order.Dominates(*gen,*binding.allocation))continue;
      std::optional<std::size_t> unit;
      if(const auto* lifetime=std::get_if<ScopeBindingLifetime>(&binding.lifetime)) {
        if(!physical)continue;const auto at=position(FlowScopeContext{lifetime->scope},scopes->regions);if(!at||at->value>=physical)continue;unit=at->value;
      } else if(const auto* lifetime=std::get_if<LoopBindingLifetime>(&binding.lifetime)) {
        if(!iterations)continue;const auto at=position(FlowLoopContext{lifetime->loop},scopes->loop_regions);if(!at||at->value>=iterations)continue;unit=physical+at->value;
      }
      result.clear[i]=1;
      if(unit) {
        const auto node=order.tree.nodes[gen->value];const SymbolId id{static_cast<std::uint32_t>(i)};
        certificates.push_back({node.begin,*unit,id,false});certificates.push_back({node.end-1,*unit,id,true});
      }
    }
    std::sort(kills.begin(),kills.end(),[&](const auto& a,const auto& b){budget.Step();return a.point<b.point;});
    std::sort(certificates.begin(),certificates.end(),[&](const auto& a,const auto& b){budget.Step();return a.point<b.point;});
    const auto update=[&](std::size_t unit,std::int64_t delta) {
      for(auto at=unit+1;at<fenwick.size();at+=at&(~at+1)){budget.Step();fenwick[at]+=delta;}
    };
    std::size_t used=0;
    for(const auto& query:certificates) {
      budget.Step();while(used<kills.size()&&kills[used].point<=query.point) {
        const auto& at=kills[used++];update(at.first,1);update(at.last,-1);
      }
      std::int64_t count=0;for(auto at=query.unit+1;at;at-=at&(~at+1)){budget.Step();count+=fenwick[at];}
      counts[query.binding.value]+=query.upper?count:-count;
    }
    for(std::size_t i=0;i<result.clear.size();++i){budget.Step();result.clear[i]&=counts[i]==0;}
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
