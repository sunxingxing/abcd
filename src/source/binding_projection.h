#ifndef ABCD_SOURCE_BINDING_PROJECTION_H_
#define ABCD_SOURCE_BINDING_PROJECTION_H_
#include "abcd/source/source_tree.h"
#include <span>
namespace abcd::source {
// Published stages compose direct early aliases, storage slots and late aliases.
// ReadValues is indexed by that final slot and cannot alter write identity.
// No storage/motion owner runs after ReadValues; no recursive lookup is needed.
struct BindingProjection {
  std::span<const std::optional<SymbolId>> aliases;
  std::span<const std::optional<SymbolId>> storage;
  std::span<const std::optional<SymbolId>> read_values;
  // A proved occurrence value takes precedence over the binding invariant.
  // Published roots are stable values; this does not remap any write.
  std::span<const std::optional<SymbolId>> read_sources;
  [[nodiscard]] SymbolId Storage(SymbolId symbol) const {
    const auto root=aliases.empty()?symbol:aliases[symbol.value].value_or(symbol);
    const auto slot=storage.empty()?root:storage[root.value].value_or(root);
    return aliases.empty()?slot:aliases[slot.value].value_or(slot);
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
