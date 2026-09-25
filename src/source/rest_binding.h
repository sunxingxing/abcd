#ifndef ABCD_SOURCE_REST_BINDING_H_
#define ABCD_SOURCE_REST_BINDING_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// The native rest owner has already proved a single prologue allocation.
// Require every local read to observe its exact initialized SSA definition.
std::optional<abc::RegisterId> FindRestLocal(
    const analysis::SemanticMethod& method, const SourceAst& ast,
    std::uint16_t first_argument);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_REST_BINDING_H_
