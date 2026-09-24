#ifndef ABCD_SOURCE_LEAF_CREATION_H_
#define ABCD_SOURCE_LEAF_CREATION_H_

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
// Private: the created-function template has no nested creation support.
// Full callee diagnostics remain available through that public API; this bridge
// reports the parent creation PC and preserves resource/output/work failures.
BodyEmitResult WriteLeafCreation(const abc::BodyInstruction& site,
                                 const MethodConstants& constants,
                                 const abc::MethodLookup& methods,
                                 std::ostream& output,
                                 LocalLexicalBudget budget);
// Typed AST bridge. Unlike the parametric form above, all external captures
// are rejected; child contexts have no leaf expansion capability.
BodyEmitResult WriteLeafFunction(const analysis::FunctionCreation& creation,
                                 abc::CodeOffset pc,
                                 const MethodConstants& constants,
                                 std::ostream& output,
                                 LocalLexicalBudget budget);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LEAF_CREATION_H_
