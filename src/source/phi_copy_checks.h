#ifndef ABCD_SOURCE_PHI_COPY_CHECKS_H_
#define ABCD_SOURCE_PHI_COPY_CHECKS_H_

#include "edge_copy_index.h"
#include "phi_copies.h"
#include "../analysis/value_flow.h"

namespace abcd::source {
inline void RequirePhiCopy(bool condition, PhiCopyError error) { if (!condition) throw error; }
inline PhiCopyError PhiScheduleError(CopyError error) {
  if (error == CopyError::kResourceLimit) return PhiCopyError::kResourceLimit;
  if (error == CopyError::kWorkLimit) return PhiCopyError::kWorkLimit;
  return PhiCopyError::kSchedule;
}
inline bool IncomingLess(const analysis::IncomingKey& a, const analysis::IncomingKey& b) {
  if (a.index() != b.index()) return std::holds_alternative<analysis::MethodEntryKey>(a);
  const auto* edge = std::get_if<analysis::EdgeId>(&a);
  return edge && *edge < std::get<analysis::EdgeId>(b);
}
inline bool SameIncoming(const analysis::IncomingKey& a, const analysis::IncomingKey& b) {
  return !IncomingLess(a, b) && !IncomingLess(b, a);
}
inline void CheckPhiFlow(const analysis::ValueProgram& graph, CopyBudget& budget) {
  const auto flow = analysis::VerifyValueFlow(graph, budget.Remaining());
  if (const auto* proof = std::get_if<analysis::ValueFlowProof>(&flow)) { budget.Step(proof->work); return; }
  const auto error = std::get<analysis::ValueAvailabilityError>(flow);
  if (error == analysis::ValueAvailabilityError::kResourceLimit) throw PhiCopyError::kResourceLimit;
  if (error == analysis::ValueAvailabilityError::kWorkLimit) throw PhiCopyError::kWorkLimit;
  throw PhiCopyError::kFlow;
}
inline bool PhiInput(const analysis::OperandUse& use) {
  return use.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueId>(use.owner);
}
inline void CheckPhiDomain(const analysis::ValueProgram& graph, const analysis::OperandUse& use) {
  const auto destination = std::get<analysis::ValueId>(use.owner);
  RequirePhiCopy(graph.values[destination.value].domain.IsLanguage(), PhiCopyError::kDomain);
  if (const auto* source = std::get_if<analysis::ValueId>(&use.operand))
    RequirePhiCopy(graph.values[source->value].domain.IsLanguage(), PhiCopyError::kDomain);
}
}  // namespace abcd::source
#endif
