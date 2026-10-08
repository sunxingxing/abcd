#include "storage_coalescing.h"
#include "recovery_scratch.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace abcd::source {
namespace {
struct ConflictOrder {
  TreeCheckBudget* budget;
  bool operator()(SymbolId a,SymbolId b) const {budget->Step();return a<b;}
};
class Coalescing {
 public:
  Coalescing(const BindingInterference& facts,std::span<const StorageBinding> bindings,
      analysis::SemanticBudget allowance):facts_(facts),bindings_(bindings),budget_(allowance),
      memory_(budget_),neighbors_(&memory_) {}
  StorageCoalescing Run() {
    budget_.Array(parents_,bindings_.size());budget_.Array(sizes_,bindings_.size());
    budget_.Array(retained_,bindings_.size());neighbors_.reserve(bindings_.size());
    budget_.Array(regions_,bindings_.size());budget_.Array(unlocated_,bindings_.size());
    budget_.Array(reachable_,bindings_.size());
    for(std::size_t i=0;i<bindings_.size();++i){budget_.Step();
      parents_[i]=SymbolId{static_cast<std::uint32_t>(i)};sizes_[i]=1;
      regions_[i]=bindings_[i].reference_region;unlocated_[i]=bindings_[i].unlocated_reference;
      reachable_[i]=bindings_[i].reachable_references;
      neighbors_.emplace_back(ConflictOrder{&budget_});}
    for(const auto& conflict:facts_.conflicts){budget_.Step();CheckPair(conflict.first,conflict.second);
      neighbors_[conflict.first.value].insert(conflict.second);
      neighbors_[conflict.second.value].insert(conflict.first);}
    for(const auto& pair:facts_.affinities){budget_.Step();CheckPair(pair.first,pair.second);
      if(!bindings_[pair.first.value].eligible||!bindings_[pair.second.value].eligible)continue;
      auto a=Find(pair.first),b=Find(pair.second);
      if(a==b||neighbors_[a.value].contains(b))continue;
      // Group construction owns interference only. Each removed group's weight
      // at least doubles, bounding repeated adjacency rewrites. Candidate
      // ranking waits for complete reference extents, never discards a member.
      if(sizes_[a.value]<sizes_[b.value])std::swap(a,b);
      const auto region=Join(regions_[a.value],regions_[b.value]);
      const bool unlocated=unlocated_[a.value]||unlocated_[b.value];
      for(auto neighbor:neighbors_[b.value]){budget_.Step();
        const auto erased=neighbors_[neighbor.value].erase(b);
        RequireTree(erased==1,TreeStructureError::kOwnership);
        neighbors_[neighbor.value].insert(a);neighbors_[a.value].insert(neighbor);}
      neighbors_[b.value].clear();parents_[b.value]=a;sizes_[a.value]+=sizes_[b.value];
      regions_[a.value]=region;unlocated_[a.value]=unlocated;
      reachable_[a.value]=Join(reachable_[a.value],reachable_[b.value]);
    }
    // Rank every member against its FINAL group. Covers can change when a
    // later union extends observations; ranking only previous winners would
    // lose a now-useful slot and make recovery depend on affinity order.
    for(std::size_t i=0;i<bindings_.size();++i) {
      budget_.Step();const SymbolId id{static_cast<std::uint32_t>(i)};
      const auto group=Find(id);auto& retained=retained_[group.value];
      retained=retained?Prefer(*retained,id,regions_[group.value],unlocated_[group.value],reachable_[group.value]):id;
    }
    StorageCoalescing result;budget_.Array(result.storage,bindings_.size());
    for(std::size_t i=0;i<bindings_.size();++i){budget_.Step();const SymbolId id{static_cast<std::uint32_t>(i)};
      const auto root=*retained_[Find(id).value];if(root!=id)result.storage[i]=root;}
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return result;
  }
 private:
  void CheckPair(SymbolId a,SymbolId b) const {
    RequireTree(a.value<bindings_.size()&&b.value<bindings_.size()&&a!=b,TreeStructureError::kBinding);
  }
  SymbolId Find(SymbolId id) {
    auto root=id;
    while(parents_[root.value]!=root){budget_.Step();root=parents_[root.value];}
    while(id!=root){budget_.Step();const auto next=parents_[id.value];parents_[id.value]=root;id=next;}
    return root;
  }
  template<class Region>
  static std::optional<Region> Join(std::optional<Region> a,std::optional<Region> b) {
    if(!a)return b;if(!b)return a;
    return Region{std::min(a->first,b->first),std::max(a->last,b->last)};
  }
  static bool Covers(const StorageBinding& binding,std::optional<SourceRegion> region,bool unlocated) {
    if(!binding.narrowed_region)return true;
    if(unlocated)return false;
    return !region||(binding.narrowed_region->first<=region->first&&binding.narrowed_region->last>=region->last);
  }
  static bool Initialized(const StorageBinding& binding,std::optional<CompletionRegion> references) {
    return !references||(binding.initialized_region&&binding.initialized_region->first<=references->first&&
        binding.initialized_region->last>=references->last);
  }
  SymbolId Prefer(SymbolId a,SymbolId b,std::optional<SourceRegion> region,bool unlocated,
      std::optional<CompletionRegion> references) {
    budget_.Step();const auto& left=bindings_[a.value];const auto& right=bindings_[b.value];
    // Reject no group here: rank each member against the final shared reference
    // extent. A wider scope alone must not bias source style.
    const bool left_covers=Covers(left,region,unlocated)&&Initialized(left,references);
    const bool right_covers=Covers(right,region,unlocated)&&Initialized(right,references);
    if(left_covers!=right_covers)return left_covers?a:b;
    if(left.unique_spelling!=right.unique_spelling)return left.unique_spelling?a:b;
    if(left.native_declaration!=right.native_declaration)return left.native_declaration?a:b;
    if(left.initialization_order!=right.initialization_order){
      if(!left.initialization_order)return b;if(!right.initialization_order)return a;
      return *left.initialization_order<*right.initialization_order?a:b;}
    return a<b?a:b;
  }
  const BindingInterference& facts_;std::span<const StorageBinding> bindings_;
  TreeCheckBudget budget_;RecoveryScratch memory_;
  std::vector<SymbolId> parents_;std::vector<std::optional<SymbolId>> retained_;std::vector<std::size_t> sizes_;
  std::vector<std::optional<SourceRegion>> regions_;std::vector<std::uint8_t> unlocated_;
  std::vector<std::optional<CompletionRegion>> reachable_;
  std::pmr::vector<std::pmr::set<SymbolId,ConflictOrder>> neighbors_;
};
}
std::variant<StorageCoalescing,TreeStructureError> PlanStorageCoalescing(
    const BindingInterference& facts,std::span<const StorageBinding> bindings,analysis::SemanticBudget allowance) {
  try{return Coalescing(facts,bindings,allowance).Run();}
  catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
