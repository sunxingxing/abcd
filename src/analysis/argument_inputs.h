#ifndef ABCD_ANALYSIS_ARGUMENT_INPUTS_H_
#define ABCD_ANALYSIS_ARGUMENT_INPUTS_H_

#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
struct ArgumentInputCheck {
  std::optional<ArgumentFrameFact> fact;
  std::uint64_t work = 0;
};
// Immutable same-body SSA required. Rechecks original opcode, frame, effects
// and read/write correspondence; value domains remain the verifier's job.
// No allocation. O(I + R + S log V), with one monotone read cursor; S snapshots
// use the existing indexed SSA write lookup. All work shares the method budget.
[[nodiscard]] std::variant<ArgumentInputCheck, SemanticProofFailure>
VerifyArgumentInputs(const abc::MethodBody& body, const SemanticMethod& method,
    std::uint64_t frame_size, std::uint32_t first_abi_register,
    const ArgumentFrameFact* arguments, SemanticBudget budget);
}  // namespace abcd::analysis
#endif
