#ifndef ABCD_SOURCE_OBJECT_INITIALIZATION_H_
#define ABCD_SOURCE_OBJECT_INITIALIZATION_H_
#include "semantic_ast_input.h"
namespace abcd::source {
// Final AST pass: annotated assignments must not be moved into other expression
// wrappers without carrying their initialization and literal-origin metadata.
void FoldObjectInitialization(const AstMethod& method, SourceAst& ast,
                              std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
