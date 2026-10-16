#ifndef ABCD_SOURCE_CLOSE_BOUNDARY_H_
#define ABCD_SOURCE_CLOSE_BOUNDARY_H_
#include "close_completion.h"
namespace abcd::source {
// Standalone only. Boundary evidence does not certify the loop/body projection,
// progress, or nested completion transfer, and grants no topology ownership.
[[nodiscard]] std::variant<CloseBoundaryProof, CloseCompletionFailure> ProveCloseBoundary(
    const abc::MethodBody&, abc::ProfileId, const analysis::ExceptionFlow&, std::uint64_t frame,
    const ExceptionPlan&, const IteratorProtocolScan&, const DerivedClose&,
    const CloseCompletionRegion&, analysis::SemanticBudget, bool reverse);
}
#endif
