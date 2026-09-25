#ifndef ABCD_SOURCE_CLASS_ASSEMBLY_H_
#define ABCD_SOURCE_CLASS_ASSEMBLY_H_

#include "semantic_ast_input.h"
#include "abcd/source/created_class.h"

namespace abcd::source {
std::optional<SourceFailure> FoldClassAssembly(
    const AstMethod& method, SourceAst& ast, std::size_t bytes);
// Resolves the retained pool keys/targets at the textual boundary. Prototype
// identity is conditional until its pool key is validated here.
CreatedClassResult WriteAssembledClass(const ClassExpression& expression,
    const MethodConstants& constants, BoundClassParent parent,
    std::ostream& output, LocalLexicalBudget budget, CreationExpansion* expansion);
}  // namespace abcd::source
#endif
