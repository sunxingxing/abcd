#ifndef ABCD_SOURCE_BINDING_NORMALIZATION_H_
#define ABCD_SOURCE_BINDING_NORMALIZATION_H_
#include "binding_projection.h"
#include "tree_checks.h"
namespace abcd::source {
struct BindingMaps {
  std::vector<std::optional<SymbolId>> aliases,storage;
};
// Canonicalize an already proved projection, never choose a new merge. An
// immutable alias supersedes the old storage association of that binding.
// Each selected edge is visited a bounded number of times; queries remain O(1).
// Empty result means the input already has terminal targets, with no allocation.
[[nodiscard]] std::optional<BindingMaps> NormalizeBindingProjection(std::size_t bindings,
    std::span<const std::optional<SymbolId>> aliases,
    std::span<const std::optional<SymbolId>> storage,TreeCheckBudget&);
}
#endif
