#ifndef ABCD_SOURCE_PARAMETER_ORIGINS_H_
#define ABCD_SOURCE_PARAMETER_ORIGINS_H_
#include "parameter_names.h"
#include "abcd/analysis/register_ssa.h"
namespace abcd::source {
struct ParameterObservation {
  analysis::InstructionId instruction;
  abc::RegisterId reg;
};
using ObservedParameters = std::vector<std::optional<std::uint32_t>>;
// Debug observations are analysis-only reads before the original instruction.
// Original Before/Commit/Catch effects and exceptional edges remain intact.
// Copy/phi agreement establishes entry identity, not source storage ownership.
[[nodiscard]] std::variant<ObservedParameters, ParameterNameError> ObserveParameters(
    const abc::FrameLayout&, const abc::MethodBody&, abc::ProfileId,
    std::span<const ParameterObservation>, std::size_t bytes);
}
#endif
