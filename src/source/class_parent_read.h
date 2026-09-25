#ifndef ABCD_SOURCE_CLASS_PARENT_READ_H_
#define ABCD_SOURCE_CLASS_PARENT_READ_H_
#include "semantic_ast_input.h"

namespace abcd::source {
void FoldClassParentReads(const AstMethod& method, SourceAst& ast,
                          std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CLASS_PARENT_READ_H_
