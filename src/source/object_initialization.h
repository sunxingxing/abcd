#ifndef ABCD_SOURCE_OBJECT_INITIALIZATION_H_
#define ABCD_SOURCE_OBJECT_INITIALIZATION_H_
#include "semantic_ast_input.h"
namespace abcd::source {
// Final expression-changing pass: result annotations may follow, but wrappers
// must carry the initialization and literal-origin metadata if introduced later.
void FoldObjectInitialization(const AstMethod& method, SourceAst& ast,
                              std::span<std::size_t> uses);
}  // namespace abcd::source
#endif
