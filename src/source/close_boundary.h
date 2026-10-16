#ifndef ABCD_SOURCE_CLOSE_BOUNDARY_H_
#define ABCD_SOURCE_CLOSE_BOUNDARY_H_
#include "close_completion.h"
#include "abcd/analysis/dominators.h"
namespace abcd::source {
struct CloseBodyIndex;
// Standalone only. Boundary evidence does not certify the loop/body projection,
// progress, or nested completion transfer, and grants no topology ownership.
[[nodiscard]] std::variant<CloseBoundaryProof, CloseCompletionFailure> ProveCloseBoundary(
    const abc::MethodBody&, abc::ProfileId, const analysis::ExceptionFlow&, std::uint64_t frame,
    const ExceptionPlan&, const IteratorProtocolScan&, const DerivedClose&,
    const CloseCompletionRegion&, analysis::SemanticBudget, bool reverse,
    const CloseBodyIndex* index = nullptr);
}
#endif
