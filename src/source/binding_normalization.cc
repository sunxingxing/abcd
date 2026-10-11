#include "binding_normalization.h"
#include <limits>
namespace abcd::source {
std::optional<BindingMaps> NormalizeBindingProjection(std::size_t bindings,
    std::span<const std::optional<SymbolId>> aliases,
    std::span<const std::optional<SymbolId>> storage,TreeCheckBudget& budget) {
  RequireTree(bindings<=std::numeric_limits<std::uint32_t>::max()&&
      (aliases.empty()||aliases.size()==bindings)&&(storage.empty()||storage.size()==bindings),TreeStructureError::kOwnership);
  for(const auto values:{aliases,storage})for(const auto target:values) {
    budget.Step();RequireTree(!target||target->value<bindings,TreeStructureError::kOwnership);
  }
  const auto next=[&](SymbolId id)->std::optional<SymbolId> {
    budget.Step();
    if(!aliases.empty())if(const auto target=aliases[id.value];target&&*target!=id)return target;
    if(!storage.empty())if(const auto target=storage[id.value];target&&*target!=id)return target;
    return {};
  };
  bool flat=true;
  for(const auto values:{aliases,storage})for(const auto target:values) {
    budget.Step();if(target&&next(*target))flat=false;
  }
  if(flat)return {};
  BindingMaps result;
  budget.Array(result.aliases,aliases.size());budget.Array(result.storage,storage.size());
  enum class State : std::uint8_t {kUnseen,kActive,kDone};
  // Iterative functional-graph traversal with path compression. A cycle is an
  // invalid representative certificate, not a restriction on source CFG loops.
  std::vector<State> states;std::vector<SymbolId> roots,path;
  budget.Array(states,bindings);budget.Array(roots,bindings);budget.Array(path,bindings);
  for(std::size_t i=0;i<bindings;++i) {
    budget.Step();if(states[i]==State::kDone)continue;
    SymbolId at{static_cast<std::uint32_t>(i)},root=at;std::size_t count=0;
    for(;;) {
      budget.Step();
      if(states[at.value]==State::kDone){root=roots[at.value];break;}
      RequireTree(states[at.value]!=State::kActive,TreeStructureError::kOwnership);
      states[at.value]=State::kActive;path[count++]=at;
      const auto target=next(at);if(!target){root=at;break;}at=*target;
    }
    while(count) {budget.Step();const auto id=path[--count];roots[id.value]=root;states[id.value]=State::kDone;}
  }
  const auto rewrite=[&](auto input,auto& output) {
    for(std::size_t i=0;i<input.size();++i) {budget.Step();if(input[i])output[i]=roots[input[i]->value];}
  };
  rewrite(aliases,result.aliases);rewrite(storage,result.storage);
  const auto scratch=states.capacity()*sizeof(State)+(roots.capacity()+path.capacity())*sizeof(SymbolId);
  std::vector<State>{}.swap(states);std::vector<SymbolId>{}.swap(roots);std::vector<SymbolId>{}.swap(path);
  budget.Release(scratch);return result;
}
}
