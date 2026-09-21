#ifndef ABCD_ANALYSIS_BRANCH_CONDITION_H_
#define ABCD_ANALYSIS_BRANCH_CONDITION_H_

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
enum class BranchError { kUnsupportedOpcode, kMalformedInstruction };

// Requires DecodeInstruction output. Returns the taken-edge predicate only;
// does not write the accumulator or resolve relative targets. The caller must
// establish a language-value input (not Hole/internal sentinels). Predicates
// have no language-level side effects. Historical equivalence is not implied.
[[nodiscard]] std::variant<UnaryExpression, BranchError> LiftBranchCondition(
    const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_BRANCH_CONDITION_H_
