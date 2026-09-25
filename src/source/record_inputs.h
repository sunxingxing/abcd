#ifndef ABCD_SOURCE_RECORD_INPUTS_H_
#define ABCD_SOURCE_RECORD_INPUTS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Record code has no source-level caller ABI. Entry values may occur only in
// pure copy chains already erased by shared SSA dead-copy elimination.
std::optional<SourceFailure> VerifyRecordInputs(
    const analysis::SemanticMethod& method, const SourceAst& ast,
    std::size_t bytes);
}  // namespace abcd::source
#endif
