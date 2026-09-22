#ifndef ABCD_SOURCE_LEAF_CREATION_H_
#define ABCD_SOURCE_LEAF_CREATION_H_

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
// Private: WriteCreatedFunction is deliberately invoked without nested support.
// Full callee diagnostics remain available through that public API; this bridge
// reports the parent creation PC and preserves resource/output/work failures.
BodyEmitResult WriteLeafCreation(const abc::BodyInstruction& site,
                                 const MethodConstants& constants,
                                 const abc::MethodLookup& methods,
                                 std::ostream& output,
                                 LocalLexicalBudget budget);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LEAF_CREATION_H_
