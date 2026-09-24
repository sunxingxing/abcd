#ifndef ABCD_SOURCE_LEAF_CREATION_H_
#define ABCD_SOURCE_LEAF_CREATION_H_

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
// Legacy decoded bridge remains leaf-only; the typed bridge below can share
// bounded nested creation admission with its synchronous parent printer.
// Full callee diagnostics remain available through that public API; this bridge
// reports the parent creation PC and preserves resource/output/work failures.
BodyEmitResult WriteLeafCreation(const abc::BodyInstruction& site,
                                 const MethodConstants& constants,
                                 const abc::MethodLookup& methods,
                                 std::ostream& output,
                                 LocalLexicalBudget budget);
// Typed AST bridge. Default rejects captures; kLanguage requires a capture
// sink whose owner proves each binding. Nested children need shared admission.
BodyEmitResult WriteLeafFunction(const analysis::FunctionCreation& creation,
                                 abc::CodeOffset pc,
                                 const MethodConstants& constants,
                                 std::ostream& output,
                                 LocalLexicalBudget budget,
                                 analysis::CapturedValues captures =
                                     analysis::CapturedValues::kReject,
                                 CreationExpansion* expansion = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LEAF_CREATION_H_
