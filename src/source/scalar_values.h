#ifndef ABCD_SOURCE_SCALAR_VALUES_H_
#define ABCD_SOURCE_SCALAR_VALUES_H_

#include "semantic_ast_input.h"

namespace abcd::source {
class CopyOrigins;
// Inline adjacent single-use private slot/primitive loads into scalar operands.
// Keep original use counts conservative and eliminated PCs as no-op anchors.
void FoldScalarValues(const AstMethod& method, SourceAst& ast,
                      std::span<const std::size_t> uses);
// Run after paired reload elimination so receiver substitution cannot hide
// the original copy pair. Getter effects remain at the consumer's PC.
// The completed copy index proves identity across single-use elided transports.
void FoldMemberReceivers(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses,
                         const CopyOrigins& copies);
}  // namespace abcd::source
#endif
