#ifndef ABCD_SOURCE_PHI_COPY_CHECKS_H_
#define ABCD_SOURCE_PHI_COPY_CHECKS_H_

#include "edge_copy_index.h"
#include "phi_copies.h"
#include "../analysis/value_flow.h"
#include "../analysis/value_phi.h"
#include "evaluation_aliases.h"

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
inline void CheckPhiFlow(const analysis::ValueProgram& graph, CopyBudget& budget, PhiFlow kind) {
  std::optional<analysis::ValueAvailabilityError> failure;
  switch (kind) {
    case PhiFlow::kNormal: {
      const auto flow = analysis::VerifyValueFlow(graph, budget.Remaining());
      if (const auto* proof = std::get_if<analysis::ValueFlowProof>(&flow)) {
        budget.Step(proof->work); return;
      }
      failure = std::get<analysis::ValueAvailabilityError>(flow); break;
    }
    case PhiFlow::kPhased: {
      const auto flow = analysis::VerifyPhasedValueFlow(graph, budget.Remaining());
      if (const auto* proof = std::get_if<analysis::PhasedValueFlowProof>(&flow)) {
        budget.Step(proof->work); return;
      }
      failure = std::get<analysis::ValueAvailabilityError>(flow); break;
    }
  }
  RequirePhiCopy(failure.has_value(), PhiCopyError::kFlow);
  const auto error = *failure;
  if (error == analysis::ValueAvailabilityError::kResourceLimit) throw PhiCopyError::kResourceLimit;
  if (error == analysis::ValueAvailabilityError::kWorkLimit) throw PhiCopyError::kWorkLimit;
  throw PhiCopyError::kFlow;
}
inline std::vector<analysis::ValueId> PhiIdentities(const analysis::ValueProgram& graph,
    CopyBudget& budget) {
  // Both flows preserve exact incoming uses. Identity transport adds no
  // mutable phi write; the same SCC equations own its source projection.
  auto indexed = analysis::AnalyzeValueIdentities(graph, budget.Remaining());
  if (const auto* error = std::get_if<analysis::ValueSimplifyError>(&indexed))
    throw *error == analysis::ValueSimplifyError::kWorkLimit ? PhiCopyError::kWorkLimit :
        *error == analysis::ValueSimplifyError::kResourceLimit ? PhiCopyError::kResourceLimit : PhiCopyError::kFlow;
  auto result = std::get<analysis::ValueIdentities>(std::move(indexed));
  budget.Admit(result.allocated_bytes,1); budget.Step(result.work);
  return std::move(result.canonical);
}
// Shared implementation behind explicit normal/phased entry points. The mode
// is checked against both the graph proof and retained plan, not caller text.
std::variant<std::uint64_t, PhiCopyError> VerifyPhiCopiesForFlow(
    const analysis::ValueProgram& graph, const PhiCopies& plan,
    analysis::SemanticBudget allowance, PhiFlow flow, std::span<const ForOfUseDisposition> for_of = {});
inline bool PhiInput(const analysis::OperandUse& use) {
  return use.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueId>(use.owner);
}
inline void CheckPhiDomain(const analysis::ValueProgram& graph, const analysis::OperandUse& use,
    PhiDomain domain) {
  const auto admitted = [domain](analysis::ValueKinds kinds) {
    return kinds.IsLanguage() || (domain == PhiDomain::kHoleTransport && !kinds.IsEmpty() &&
        !kinds.Contains(analysis::VmValueKind::kEnvironment) && !kinds.Contains(analysis::VmValueKind::kOtherInternal) &&
        !kinds.Contains(analysis::VmValueKind::kPropertyEnumerator));
  };
  const auto destination = std::get<analysis::ValueId>(use.owner);
  RequirePhiCopy(admitted(graph.values[destination.value].domain), PhiCopyError::kDomain);
  if (const auto* source = std::get_if<analysis::ValueId>(&use.operand))
    RequirePhiCopy(admitted(graph.values[source->value].domain), PhiCopyError::kDomain);
}
}  // namespace abcd::source
#endif
