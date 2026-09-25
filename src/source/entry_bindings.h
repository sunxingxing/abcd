#ifndef ABCD_SOURCE_ENTRY_BINDINGS_H_
#define ABCD_SOURCE_ENTRY_BINDINGS_H_

#include "abcd/source/scalar_expression.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::source {
struct EntryBinding {
  std::optional<abc::RegisterId> source;
  analysis::SsaValueId definition;
};
// Caller supplies one admitted entry per register. All candidates share the
// SSA scans; never run a per-parameter graph walk. Input slots are independently
// certified immutable by the callable owner. This does not erase declarations.
void FindEntryBindings(const analysis::SemanticMethod& method,
                       ScalarNames names,
                       std::span<EntryBinding> bindings);
void BindEntryReads(analysis::ScalarExpression& expression,
                    std::span<const EntryBinding> bindings);
}  // namespace abcd::source
#endif
