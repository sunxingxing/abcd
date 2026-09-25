#ifndef ABCD_SOURCE_AWAIT_POSITIONS_H_
#define ABCD_SOURCE_AWAIT_POSITIONS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Append semantic aliases only after normal spans/fused distances are final.
// Original protocol PCs travel through AwaitValue IR and SourceAssignment.
// Budget includes existing text/spans, additional spans and transient scratch.
std::optional<SourceFailure> AppendAwaitPositions(
    const SourceAst& ast, PrintedSource& source, std::size_t bytes);
}  // namespace abcd::source
#endif
