#ifndef ABCD_SOURCE_DEFINED_CAPTURES_H_
#define ABCD_SOURCE_DEFINED_CAPTURES_H_

#include "abcd/analysis/semantic_validation.h"
#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Additional same-body owner proof, never a generic captured-value premise.
std::variant<analysis::DefinedCaptures, SourceFailure> ProveDefinedCaptures(
    const abc::MethodBody& body, const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, std::uint64_t frame_size,
    const MethodConstants& constants, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
