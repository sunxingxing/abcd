#include "binding_interference.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error=TreeStructureError;
template<class Accept> void CopyPreferences(const RecoveryFlow& flow,TreeCheckBudget& budget,Accept accept) {
  for(const auto& definition:flow.definitions) {
    budget.Step();const auto copied=definition.copy;
    if(copied&&*copied!=definition.binding&&
        flow.bindings[copied->value].kind==BindingKind::kCompilerTemporary&&
        flow.bindings[definition.binding.value].kind==BindingKind::kCompilerTemporary)
      accept(BindingPreference{*copied,definition.binding});
  }
}
class Interference {
 public:
  Interference(const RecoveryFlow& flow,const FlowLiveness& liveness,analysis::SemanticBudget budget,std::span<const BindingPreference> preferences)
      :flow_(flow),liveness_(liveness),preferences_(preferences),budget_(budget),memory_(budget_),proposals_(&memory_),conflicts_(&memory_) {}
  BindingInterference Run() {
    CopyPreferences(flow_,budget_,[&](BindingPreference pair){proposals_.push_back(Pair(pair.first,pair.second));});
    for(const auto pair:preferences_) {
      budget_.Step();RequireTree(pair.first.value<flow_.bindings.size()&&pair.second.value<flow_.bindings.size(),Error::kBinding);
      if(pair.first!=pair.second&&flow_.bindings[pair.first.value].kind==BindingKind::kCompilerTemporary&&
          flow_.bindings[pair.second.value].kind==BindingKind::kCompilerTemporary)
        proposals_.push_back(Pair(pair.first,pair.second));
    }
    const auto less=[&](const BindingAffinity& a,const BindingAffinity& b){budget_.Step();return Key(a)<Key(b);};
    std::sort(proposals_.begin(),proposals_.end(),less);
    proposals_.erase(std::unique(proposals_.begin(),proposals_.end(),[&](auto a,auto b){budget_.Step();return Key(a)==Key(b);}),proposals_.end());
    MarkInterference();
    std::sort(conflicts_.begin(),conflicts_.end(),[&](auto a,auto b){budget_.Step();return a<b;});
    conflicts_.erase(std::unique(conflicts_.begin(),conflicts_.end(),[&](auto a,auto b){budget_.Step();return a==b;}),conflicts_.end());
    std::size_t conflict=0;
    for(auto& pair:proposals_) {
      budget_.Step();const BindingConflict key{pair.first,pair.second};
      while(conflict<conflicts_.size()&&conflicts_[conflict]<key){budget_.Step();++conflict;}
      pair.interference=conflict<conflicts_.size()&&conflicts_[conflict]==key;
    }
    BindingInterference result;budget_.Array(result.affinities,proposals_.size());
    std::copy(proposals_.begin(),proposals_.end(),result.affinities.begin());
    budget_.Array(result.conflicts,conflicts_.size());
    std::copy(conflicts_.begin(),conflicts_.end(),result.conflicts.begin());
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return result;
  }
 private:
  static std::pair<SymbolId,SymbolId> Key(const BindingAffinity& pair){return {pair.first,pair.second};}
  static BindingAffinity Pair(SymbolId a,SymbolId b){return a<b?BindingAffinity{a,b}:BindingAffinity{b,a};}
  void MarkInterference() {
    for(const auto& fact:liveness_.live_in) {
      budget_.Step();
      for(auto edge=flow_.events[fact.point.value].incoming;edge;edge=flow_.edges[*edge].incoming) {
        budget_.Step();const auto predecessor=flow_.edges[*edge].from;const auto& event=flow_.events[predecessor.value];
        const auto* definition=event.definition?&flow_.definitions[event.definition->value]:nullptr;
        if(definition&&definition->binding!=fact.binding&&definition->copy!=fact.binding&&
            flow_.bindings[definition->binding.value].kind==BindingKind::kCompilerTemporary&&
            flow_.bindings[fact.binding.value].kind==BindingKind::kCompilerTemporary) {
          const auto pair=Pair(definition->binding,fact.binding);
          conflicts_.push_back({pair.first,pair.second});
        }
      }
    }
  }
  const RecoveryFlow& flow_;const FlowLiveness& liveness_;std::span<const BindingPreference> preferences_;TreeCheckBudget budget_;RecoveryScratch memory_;
  std::pmr::vector<BindingAffinity> proposals_;
  std::pmr::vector<BindingConflict> conflicts_;
};
}
std::variant<BindingPreferences,TreeStructureError> FindBindingPreferences(const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);BindingPreferences result;
    std::size_t count=0;CopyPreferences(flow,budget,[&](BindingPreference){++count;});
    budget.Array(result.pairs,count);count=0;
    CopyPreferences(flow,budget,[&](BindingPreference pair){result.pairs[count++]=pair;});
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  }catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
std::variant<BindingInterference,TreeStructureError> FindBindingInterference(
    const RecoveryFlow& flow,const FlowLiveness& liveness,analysis::SemanticBudget budget,std::span<const BindingPreference> preferences) {
  try{return Interference(flow,liveness,budget,preferences).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
