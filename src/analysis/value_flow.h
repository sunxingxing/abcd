#ifndef ABCD_ANALYSIS_VALUE_FLOW_H_
#define ABCD_ANALYSIS_VALUE_FLOW_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::analysis {
enum class ValueAvailabilityError : std::uint8_t {
  kInvalidGraph, kInvalidTerminator, kInvalidPhi, kUnavailableValue,
  kUnreachableBlock, kResumeFlow, kWorkLimit, kResourceLimit
};
struct ValueFlowProof {
  IrRevision revision;
  CfgRevision cfg_revision;
  std::uint64_t work;
};
// Normal-flow availability only, not an effects/input-identity certificate.
// Budget covers additional verification scratch; graph storage belongs to caller.
// Tombstone orders stay stable. Unknown entries remain VM values, not JS locals.
[[nodiscard]] std::variant<ValueFlowProof, ValueAvailabilityError> VerifyValueFlow(
    const ValueProgram& graph, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
