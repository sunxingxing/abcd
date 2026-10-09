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
      static_assert(std::is_same_v<T,ScopeId>);
      if(unit.value>=regions.size()||scope.value>=regions.size())return FlowUnitPresence::kUnmodeled;
      const auto& region=regions[unit.value];const auto& nested=regions[scope.value];
      return region.first<=nested.first&&nested.first<region.last?FlowUnitPresence::kActive:FlowUnitPresence::kInactive;
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
std::shared_ptr<FlowScopes> MakeFlowScopes(const FunctionTree& tree,TreeCheckBudget& budget) {
  auto result=std::allocate_shared<FlowScopes>(RecoveryAllocator<FlowScopes>{budget});
  budget.Array(result->regions,tree.scopes.size());RecoveryScratch memory(budget);
  std::pmr::vector<std::optional<ScopeId>> heads(&memory),next(&memory);
  std::pmr::vector<std::uint8_t> seen(&memory);
  struct Cursor {ScopeId scope;std::optional<ScopeId> child;};
  std::pmr::vector<Cursor> pending(&memory);
  budget.Step(tree.scopes.size());heads.resize(tree.scopes.size());next.resize(tree.scopes.size());seen.resize(tree.scopes.size());
  RequireTree(tree.scopes.size()<=std::numeric_limits<std::uint32_t>::max(),TreeStructureError::kResourceLimit);
  for(std::size_t i=0;i<tree.scopes.size();++i) {
    budget.Step();if(const auto parent=tree.scopes[i].parent) {
      RequireTree(parent->value<tree.scopes.size(),TreeStructureError::kScope);
      const ScopeId child{static_cast<std::uint32_t>(i)};next[i]=heads[parent->value];heads[parent->value]=child;
    }
  }
  std::size_t position=0;
  const auto enter=[&](ScopeId scope) {
    RequireTree(!seen[scope.value],TreeStructureError::kScope);seen[scope.value]=1;
    result->regions[scope.value].first={position++};pending.push_back({scope,heads[scope.value]});
  };
  for(std::size_t i=0;i<tree.scopes.size();++i) {
    budget.Step();if(tree.scopes[i].parent)continue;enter({static_cast<std::uint32_t>(i)});
    while(!pending.empty()) {
      budget.Step();auto& cursor=pending.back();
      if(const auto child=cursor.child){cursor.child=next[child->value];enter(*child);}
      else {result->regions[cursor.scope.value].last={position};pending.pop_back();}
    }
  }
  RequireTree(position==tree.scopes.size(),TreeStructureError::kScope);return result;
}
}
