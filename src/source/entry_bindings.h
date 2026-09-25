#ifndef ABCD_SOURCE_ENTRY_BINDINGS_H_
#define ABCD_SOURCE_ENTRY_BINDINGS_H_

#include "abcd/source/scalar_expression.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::source {
struct SourceAst;
struct EntryBinding {
  std::optional<abc::RegisterId> source;
  analysis::SsaValueId definition;
  std::optional<analysis::SsaValueId> initialized;
};
// Caller supplies one admitted entry per register. All candidates share the
// SSA scans; never run a per-parameter graph walk. Input slots are independently
// certified immutable by the callable owner. Recovered literal defaults admit
// their copy/initialization phi web, but never an entry seed or another write.
// Final AST commits are audited after SSA: retargeting can change slot writes.
// Declaration removal remains the common printer's emitted-slot census.
void FindEntryBindings(const analysis::SemanticMethod& method,
                       const SourceAst& ast, ScalarNames names,
                       std::span<EntryBinding> bindings);
void BindEntryReads(analysis::ScalarExpression& expression,
                    std::span<const EntryBinding> bindings);
}  // namespace abcd::source
#endif
