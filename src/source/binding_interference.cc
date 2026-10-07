#include "binding_interference.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error=TreeStructureError;
struct Live {FlowPoint point;SymbolId symbol;auto operator<=>(const Live&) const = default;};
class Interference {
 public:
  Interference(const RecoveryFlow& flow,analysis::SemanticBudget budget)
      :flow_(flow),budget_(budget),memory_(budget_),proposals_(&memory_) {}
  BindingInterference Run() {
    for(const auto& definition:flow_.definitions) {
      budget_.Step();const auto copied=definition.copy;
      if(copied&&*copied!=definition.binding&&
          flow_.bindings[copied->value]==BindingKind::kCompilerTemporary&&
          flow_.bindings[definition.binding.value]==BindingKind::kCompilerTemporary)
        proposals_.push_back(Pair(*copied,definition.binding));
    }
    const auto less=[&](const BindingAffinity& a,const BindingAffinity& b){budget_.Step();return Key(a)<Key(b);};
    std::sort(proposals_.begin(),proposals_.end(),less);
    proposals_.erase(std::unique(proposals_.begin(),proposals_.end(),[&](auto a,auto b){budget_.Step();return Key(a)==Key(b);}),proposals_.end());
    if(!proposals_.empty())Liveness();
    BindingInterference result;budget_.Array(result.affinities,proposals_.size());
    std::copy(proposals_.begin(),proposals_.end(),result.affinities.begin());
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return result;
  }
 private:
  static std::pair<SymbolId,SymbolId> Key(const BindingAffinity& pair){return {pair.first,pair.second};}
  static BindingAffinity Pair(SymbolId a,SymbolId b){return a<b?BindingAffinity{a,b}:BindingAffinity{b,a};}
  void Liveness() {
    std::pmr::vector<std::uint8_t> active(flow_.bindings.size(),0,&memory_);
    for(const auto& pair:proposals_){budget_.Step();active[pair.first.value]=active[pair.second.value]=1;}
    const auto less=[&](const Live& a,const Live& b){budget_.Step();return a<b;};
    std::pmr::set<Live,decltype(less)> facts(less,&memory_);std::pmr::vector<Live> pending(&memory_);
    const auto add=[&](Live fact){if(facts.insert(fact).second)pending.push_back(fact);};
    for(std::size_t i=0;i<flow_.events.size();++i){budget_.Step();if(flow_.events[i].read&&active[flow_.events[i].read->value])add({{static_cast<std::uint32_t>(i)},*flow_.events[i].read});}
    while(!pending.empty()) {
      const auto fact=pending.back();pending.pop_back();budget_.Step();
      for(auto edge=flow_.events[fact.point.value].incoming;edge;edge=flow_.edges[*edge].incoming) {
        budget_.Step();const auto predecessor=flow_.edges[*edge].from;const auto& event=flow_.events[predecessor.value];
        const auto* definition=event.definition?&flow_.definitions[event.definition->value]:nullptr;
        if(definition&&definition->binding!=fact.symbol&&definition->copy!=fact.symbol) {
          const auto pair=Pair(definition->binding,fact.symbol);
          const auto at=std::lower_bound(proposals_.begin(),proposals_.end(),pair,[&](auto a,auto b){budget_.Step();return Key(a)<Key(b);});
          if(at!=proposals_.end()&&Key(*at)==Key(pair))at->interference=true;
        }
        if(!definition||definition->binding!=fact.symbol)add({predecessor,fact.symbol});
      }
    }
  }
  const RecoveryFlow& flow_;TreeCheckBudget budget_;RecoveryScratch memory_;
  std::pmr::vector<BindingAffinity> proposals_;
};
}
std::variant<BindingInterference,TreeStructureError> FindBindingInterference(
    const RecoveryFlow& flow,analysis::SemanticBudget budget) {
  try{return Interference(flow,budget).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
