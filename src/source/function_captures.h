#ifndef ABCD_SOURCE_FUNCTION_CAPTURES_H_
#define ABCD_SOURCE_FUNCTION_CAPTURES_H_

#include "abcd/analysis/value_environment.h"
#include "function_input.h"

namespace abcd::source {
enum class FunctionCaptureError : std::uint8_t {
  kEnvironment, kOrigin, kCoverage, kBinding, kResourceLimit, kWorkLimit
};
// Check incoming cell occurrences after work-graph rewrites against immutable
// input. IDs are connected through original instruction origins, never by
// comparing EnvironmentIds from different indexes or by matching source names.
// This is correspondence only, not parent initialization or child completion.
[[nodiscard]] std::variant<std::uint64_t, FunctionCaptureError> VerifyFunctionCaptures(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const analysis::ValueEnvironments& environments, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
