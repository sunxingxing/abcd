#ifndef ABCD_SOURCE_SCALAR_CONSTANTS_H_
#define ABCD_SOURCE_SCALAR_CONSTANTS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Inline adjacent single-use primitive loads into typed scalar operands.
// Keep original use counts conservative and eliminated PCs as no-op anchors.
void FoldScalarConstants(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
