#ifndef ABCD_ANALYSIS_VALUE_FLOW_H_
#define ABCD_ANALYSIS_VALUE_FLOW_H_

#include "abcd/analysis/value_ir.h"
#include "abcd/analysis/dominators.h"

namespace abcd::analysis {
enum class ValueAvailabilityError : std::uint8_t {
  kInvalidGraph, kInvalidTerminator, kInvalidPhi, kUnavailableValue,
  kUnreachableBlock, kResumeFlow, kWorkLimit, kResourceLimit
};
struct ValueFlowProof {
  IrRevision revision;
  CfgRevision cfg_revision;
  std::uint64_t work;
  // Conservative simultaneous scratch charge, including dominator peak.
  std::size_t allocated_bytes = 0;
  DominatorTree dominators;
};
struct PhasedValueFlowProof {
  IrRevision revision;
  CfgRevision cfg_revision;
  std::uint64_t work;
  std::size_t allocated_bytes = 0;
  DominatorTree dominators;
};
// Query an additional evaluation site using a fresh proof of this graph.
// Availability is after a successful Commit and at phi/catch block entry;
// it does not authorize moving effects or reading mutable source storage.
[[nodiscard]] bool ValueAvailableAt(const ValueProgram& graph,
    const DominatorTree& dominators, ValueId value, WorkBlockId block,
    std::uint32_t order);
// Availability across Before/Commit/Catch, not input/handler correspondence or
// permission to publish source. Only explicit throw's unreachable, valueless
// Commit is admitted; other unreachable phases still reject.
[[nodiscard]] std::variant<PhasedValueFlowProof, ValueAvailabilityError> VerifyPhasedValueFlow(
    const ValueProgram& graph, SemanticBudget budget = {});
// Normal-flow availability only, not an effects/input-identity certificate.
// Budget covers additional verification scratch; graph storage belongs to caller.
// Tombstone orders stay stable. Unknown entries remain VM values, not JS locals.
[[nodiscard]] std::variant<ValueFlowProof, ValueAvailabilityError> VerifyValueFlow(
    const ValueProgram& graph, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
