#ifndef ABCD_SOURCE_SEMANTIC_FIELDS_H_
#define ABCD_SOURCE_SEMANTIC_FIELDS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Completes SourceOwnStore's allocation proof with immutable pool shape facts.
std::optional<SourceFailure> VerifyOwnFields(
    const SourceAst& ast, const MethodConstants& constants,
    ObjectBufferBudget budget = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_FIELDS_H_
