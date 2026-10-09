#include "flow_scopes.h"
#include "recovery_flow.h"
#include "recovery_scratch.h"
#include <limits>
namespace abcd::source {
FlowUnitPresence FlowScopes::Presence(ScopeId unit,FlowPoint point,FlowScopePhase phase) const {
  if(point.value>=events.size())return FlowUnitPresence::kUnmodeled;
  const auto& context=phase==FlowScopePhase::kBefore?events[point.value].before:events[point.value].after;
  return std::visit([&](const auto& scope) {
    using T=std::decay_t<decltype(scope)>;
    if constexpr(std::is_same_v<T,CompletionOnlyScope>)return FlowUnitPresence::kCompletionOnly;
    else if constexpr(std::is_same_v<T,IncomingScope>)return FlowUnitPresence::kInactive;
    else if constexpr(std::is_same_v<T,UnmodeledScope>)return FlowUnitPresence::kUnmodeled;
    else {
      static_assert(std::is_same_v<T,ScopeId>||std::is_same_v<T,EnclosingScope>);
      const auto active=[&]{if constexpr(std::is_same_v<T,ScopeId>)return scope;else return scope.scope;}();
      if(unit.value>=regions.size()||active.value>=regions.size())return FlowUnitPresence::kUnmodeled;
      const auto& region=regions[unit.value];const auto& nested=regions[active.value];
      if(region.first<=nested.first&&nested.first<region.last)return FlowUnitPresence::kActive;
      if constexpr(std::is_same_v<T,EnclosingScope>)return FlowUnitPresence::kUnmodeled;
      else return FlowUnitPresence::kInactive;
    }
  },context);
}
FlowUnitTransfer FlowScopes::Transfer(ScopeId unit,FlowPoint from,FlowPoint to) const {
  const auto prior=Presence(unit,from,FlowScopePhase::kAfter),next=Presence(unit,to,FlowScopePhase::kBefore);
  if(prior==FlowUnitPresence::kInactive||next==FlowUnitPresence::kInactive)return FlowUnitTransfer::kEnded;
  if(prior==FlowUnitPresence::kActive&&next==FlowUnitPresence::kActive)return FlowUnitTransfer::kPreserved;
  if(prior==FlowUnitPresence::kCompletionOnly&&next==FlowUnitPresence::kCompletionOnly)return FlowUnitTransfer::kPreserved;
  return FlowUnitTransfer::kUnmodeled;
}
namespace {
template<class Id,class Parent,class Present>
void IndexRegions(std::size_t size,const Parent& parent,const Present& present,
    std::vector<FlowScopeRegion>& regions,std::vector<FlowScopePath>& paths,TreeCheckBudget& budget) {
  budget.Array(regions,size);RecoveryScratch memory(budget);
  std::pmr::vector<std::optional<Id>> heads(&memory),next(&memory);
  std::pmr::vector<std::uint8_t> seen(&memory);
  struct Cursor {Id scope;std::optional<Id> child;};
  std::pmr::vector<Cursor> pending(&memory);
  budget.Step(size);heads.resize(size);next.resize(size);seen.resize(size);
  RequireTree(size<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
  std::size_t members=0;
  for(std::size_t i=0;i<size;++i) {
    budget.Step();if(!present(i))continue;++members;
    if(const auto enclosing=parent(i)) {
      RequireTree(enclosing->value<size&&present(enclosing->value),TreeStructureError::kScope);
      const Id child{static_cast<std::uint32_t>(i)};next[i]=heads[enclosing->value];heads[enclosing->value]=child;
    }
  }
  std::size_t position=0;
  const auto enter=[&](Id scope) {
    RequireTree(!seen[scope.value],TreeStructureError::kScope);seen[scope.value]=1;
    regions[scope.value].first={position++};pending.push_back({scope,heads[scope.value]});
  };
  for(std::size_t i=0;i<size;++i) {
    budget.Step();if(!present(i)||parent(i))continue;enter({static_cast<std::uint32_t>(i)});
    while(!pending.empty()) {
      budget.Step();auto& cursor=pending.back();
      if(const auto child=cursor.child){cursor.child=next[child->value];enter(*child);}
      else {regions[cursor.scope.value].last={position};pending.pop_back();}
    }
  }
  RequireTree(position==members,TreeStructureError::kScope);
  // Largest-subtree heavy child leaves at most half the remaining nodes on
  // each light hop. A second iterative preorder preserves Euler containment.
  std::pmr::vector<std::optional<Id>> heavy(&memory);
  std::pmr::vector<std::size_t> widths(&memory);budget.Step(size);heavy.resize(size);widths.resize(size);
  for(std::size_t i=0;i<size;++i){budget.Step();widths[i]=regions[i].last.value-regions[i].first.value;}
  for(std::size_t i=0;i<size;++i)for(auto child=heads[i];child;child=next[child->value]) {
    budget.Step();if(!heavy[i]||widths[child->value]>widths[heavy[i]->value])heavy[i]=child;
  }
  struct PathEntry {Id unit;std::optional<ScopePosition> parent,head;std::uint32_t depth;};
  std::pmr::vector<PathEntry> entries(&memory);budget.Array(paths,members);position=0;
  for(std::size_t i=0;i<size;++i) {
    budget.Step();if(!present(i)||parent(i))continue;entries.push_back({Id{static_cast<std::uint32_t>(i)},{},{},0});
    while(!entries.empty()) {
      budget.Step();const auto at=entries.back();entries.pop_back();const ScopePosition index{position++};
      const auto head=at.head.value_or(index);paths[index.value]={at.parent,head,at.depth};
      regions[at.unit.value]={index,{index.value+widths[at.unit.value]}};
      for(auto child=heads[at.unit.value];child;child=next[child->value]) {
        budget.Step();if(child!=heavy[at.unit.value])entries.push_back({*child,index,{},at.depth+1});
      }
      if(const auto child=heavy[at.unit.value])entries.push_back({*child,index,head,at.depth+1});
    }
  }
  RequireTree(position==members,TreeStructureError::kScope);
}
FlowUnitTransfer UnitTransfer(FlowUnitPresence prior,FlowUnitPresence next) {
  if(prior==FlowUnitPresence::kInactive||next==FlowUnitPresence::kInactive)return FlowUnitTransfer::kEnded;
  if(prior==FlowUnitPresence::kActive&&next==FlowUnitPresence::kActive)return FlowUnitTransfer::kPreserved;
  return FlowUnitTransfer::kUnmodeled;
}
}
FlowUnitPresence FlowScopes::LoopPresence(StmtId unit,FlowPoint point,FlowScopePhase phase) const {
  if(point.value>=loop_events.size()||unit.value>=loop_regions.size()||!(loop_regions[unit.value].first<loop_regions[unit.value].last))return FlowUnitPresence::kUnmodeled;
  const auto& context=phase==FlowScopePhase::kBefore?loop_events[point.value].before:loop_events[point.value].after;
  return std::visit([&](const auto& scope) {
    using T=std::decay_t<decltype(scope)>;
    if constexpr(std::is_same_v<T,IncomingScope>)return FlowUnitPresence::kInactive;
    else if constexpr(std::is_same_v<T,UnmodeledScope>)return FlowUnitPresence::kUnmodeled;
    else {
      static_assert(std::is_same_v<T,StmtId>||std::is_same_v<T,ExitedLoopScope>);
      const auto id=[&]{if constexpr(std::is_same_v<T,StmtId>)return scope;else return scope.loop;}();
      if(id.value>=loop_regions.size()||!(loop_regions[id.value].first<loop_regions[id.value].last))return FlowUnitPresence::kUnmodeled;
      const auto& region=loop_regions[unit.value];const auto& nested=loop_regions[id.value];
      if constexpr(std::is_same_v<T,ExitedLoopScope>) {
        if(nested.first<=region.first&&region.first<nested.last)return FlowUnitPresence::kInactive;
      }
      if(region.first<=nested.first&&nested.first<region.last)return FlowUnitPresence::kActive;
      return FlowUnitPresence::kUnmodeled;
    }
  },context);
}
FlowUnitTransfer FlowScopes::LoopTransfer(StmtId unit,FlowPoint from,FlowPoint to) const {
  return UnitTransfer(LoopPresence(unit,from,FlowScopePhase::kAfter),LoopPresence(unit,to,FlowScopePhase::kBefore));
}
std::shared_ptr<FlowScopes> MakeFlowScopes(const FunctionTree& tree,TreeCheckBudget& budget) {
  auto result=std::allocate_shared<FlowScopes>(RecoveryAllocator<FlowScopes>{budget});
  IndexRegions<ScopeId>(tree.scopes.size(),[&](std::size_t i){return tree.scopes[i].parent;},[](std::size_t){return true;},result->regions,result->paths,budget);
  return result;
}
void IndexFlowLoopScopes(FlowScopes& result,std::span<const std::optional<FlowLoopScope>> scopes,TreeCheckBudget& budget) {
  IndexRegions<StmtId>(scopes.size(),[&](std::size_t i){return scopes[i]?scopes[i]->parent:std::nullopt;},[&](std::size_t i){return scopes[i].has_value();},result.loop_regions,result.loop_paths,budget);
}
}
