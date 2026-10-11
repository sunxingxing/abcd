#ifndef ABCD_SOURCE_BINDING_PROJECTION_H_
#define ABCD_SOURCE_BINDING_PROJECTION_H_
#include "abcd/source/source_tree.h"
#include <span>
namespace abcd::source {
// Normalized maps have terminal targets, independent of the number of passes.
// Publication and the shared projection/flow boundaries own normalization.
// ReadValues is indexed by that final slot and cannot alter write identity.
struct BindingProjection {
  std::span<const std::optional<SymbolId>> aliases;
  std::span<const std::optional<SymbolId>> storage;
  std::span<const std::optional<SymbolId>> read_values;
  // A proved occurrence value takes precedence over the binding invariant.
  // Published identities are proved at this read; this does not remap any write.
  std::span<const std::optional<SymbolId>> read_sources;
  [[nodiscard]] SymbolId Storage(SymbolId symbol) const {
    if(!aliases.empty())if(const auto root=aliases[symbol.value];root&&*root!=symbol)return *root;
    return storage.empty()?symbol:storage[symbol.value].value_or(symbol);
  }
  [[nodiscard]] SymbolId Read(SymbolId symbol) const {
    const auto slot=Storage(symbol);
    return read_values.empty()?slot:read_values[slot.value].value_or(slot);
  }
  [[nodiscard]] SymbolId Read(ExprId expression,SymbolId symbol) const {
    if(!read_sources.empty()&&read_sources[expression.value])return *read_sources[expression.value];
    return Read(symbol);
  }
};
}  // namespace abcd::source
#endif
