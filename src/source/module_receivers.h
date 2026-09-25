#ifndef ABCD_SOURCE_MODULE_RECEIVERS_H_
#define ABCD_SOURCE_MODULE_RECEIVERS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Same-block module load into the immediately following staged property read,
// crossing only effect-free empty nodes. Preserve the load's source origin.
void FoldModuleReceivers(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_RECEIVERS_H_
