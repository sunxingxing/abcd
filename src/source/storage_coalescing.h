#ifndef ABCD_SOURCE_STORAGE_COALESCING_H_
#define ABCD_SOURCE_STORAGE_COALESCING_H_
#include "binding_interference.h"
namespace abcd::source {
// Half-open preorder interval in the shared ALL-completion dominator tree.
// Distinct from SourceRegion: execution availability is not lexical extent.
struct CompletionRegion {std::uint32_t first=0,last=0;};
struct StorageBinding {
  bool eligible=false;
  bool native_declaration=false;
  // Current completion dominator preorder, only a representative preference.
  // This rank does not prove initialization, visibility or source lifetime.
  std::optional<std::size_t> initialization_order;
  // Prefer a spelling without another binding in this checked tree. This
  // improves representative choice; it is never an admission requirement.
  bool unique_spelling=true;
  // Shared original/projected binding region and actual reference extent, only
  // preferences. Absence adds no known containment/observation. Complete
  // initialization, name and lifetime proofs still follow the proposal.
  std::optional<SourceRegion> binding_region;
  std::optional<SourceRegion> reference_region;
  bool unlocated_reference=false;
  std::optional<CompletionRegion> initialized_region;
  std::optional<CompletionRegion> reachable_references;
};
struct StorageShadow {SymbolId binding;std::optional<SourceRegion> region;};
struct StorageReference {SymbolId binding;SourcePosition position;};
// Common canonical spelling facts and actual current reference positions.
// This is a representative preference, not an extra storage admission.
struct StorageNames {
  std::span<const StorageShadow> shadows;
  std::span<const StorageReference> references;
};
struct StorageCoalescing {
  std::vector<std::optional<SymbolId>> storage;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Greedy interference-graph contraction with union by size and indexed conflict
// adjacency. Each accepted group has no internal conflict; output slots are
// direct representatives. Rank ALL original members against completed group
// extents after contraction; do not choose/discard candidates during unions.
// Semantic scope/initialization checks follow this pure storage proposal and
// may retain any subset of its members independently.
[[nodiscard]] std::variant<StorageCoalescing,TreeStructureError> PlanStorageCoalescing(
    const BindingInterference&,std::span<const StorageBinding>,analysis::SemanticBudget = {},StorageNames = {});
}
#endif
