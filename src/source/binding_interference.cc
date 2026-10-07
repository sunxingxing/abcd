#include "binding_interference.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error=TreeStructureError;
class Interference {
 public:
  Interference(const RecoveryFlow& flow,const FlowLiveness& liveness,analysis::SemanticBudget budget)
      :flow_(flow),liveness_(liveness),budget_(budget),memory_(budget_),proposals_(&memory_) {}
  BindingInterference Run() {
    for(const auto& definition:flow_.definitions) {
      budget_.Step();const auto copied=definition.copy;
      if(copied&&*copied!=definition.binding&&
          flow_.bindings[copied->value].kind==BindingKind::kCompilerTemporary&&
          flow_.bindings[definition.binding.value].kind==BindingKind::kCompilerTemporary)
        proposals_.push_back(Pair(*copied,definition.binding));
    }
    const auto less=[&](const BindingAffinity& a,const BindingAffinity& b){budget_.Step();return Key(a)<Key(b);};
    std::sort(proposals_.begin(),proposals_.end(),less);
    proposals_.erase(std::unique(proposals_.begin(),proposals_.end(),[&](auto a,auto b){budget_.Step();return Key(a)==Key(b);}),proposals_.end());
    if(!proposals_.empty())MarkInterference();
    BindingInterference result;budget_.Array(result.affinities,proposals_.size());
    std::copy(proposals_.begin(),proposals_.end(),result.affinities.begin());
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
        if(definition&&definition->binding!=fact.binding&&definition->copy!=fact.binding) {
          const auto pair=Pair(definition->binding,fact.binding);
          const auto at=std::lower_bound(proposals_.begin(),proposals_.end(),pair,[&](auto a,auto b){budget_.Step();return Key(a)<Key(b);});
          if(at!=proposals_.end()&&Key(*at)==Key(pair))at->interference=true;
        }
      }
    }
  }
  const RecoveryFlow& flow_;const FlowLiveness& liveness_;TreeCheckBudget budget_;RecoveryScratch memory_;
  std::pmr::vector<BindingAffinity> proposals_;
};
}
std::variant<BindingInterference,TreeStructureError> FindBindingInterference(
    const RecoveryFlow& flow,const FlowLiveness& liveness,analysis::SemanticBudget budget) {
  try{return Interference(flow,liveness,budget).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
