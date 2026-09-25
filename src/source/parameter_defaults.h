#ifndef ABCD_SOURCE_PARAMETER_DEFAULTS_H_
#define ABCD_SOURCE_PARAMETER_DEFAULTS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Same original, verified normal method and its fully lowered AST. Literal
// defaults only; unsupported evidence leaves both signature and body intact.
std::optional<SourceFailure> RecoverParameterDefaults(
    const analysis::SemanticMethod& method, const abc::FrameLayout& frame,
    std::uint16_t length, SourceAst& ast, std::size_t bytes);
}  // namespace abcd::source
#endif
