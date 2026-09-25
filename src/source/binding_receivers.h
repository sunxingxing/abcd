#ifndef ABCD_SOURCE_BINDING_RECEIVERS_H_
#define ABCD_SOURCE_BINDING_RECEIVERS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Same-block binding load into the immediately following staged property read,
// crossing only effect-free empty nodes. Preserve the load's source origin.
void FoldBindingReceivers(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_BINDING_RECEIVERS_H_
