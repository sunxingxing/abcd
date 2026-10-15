#ifndef ABCD_SOURCE_CONCURRENT_BOUNDARY_H_
#define ABCD_SOURCE_CONCURRENT_BOUNDARY_H_
#include "cell_plan.h"
#include "abcd/source/concurrent_failure.h"
namespace abcd::source {
// Consume the already verified, descendant-inclusive storage summary. A pure
// forwarded demand is a boundary capture even without a body lexical access.
std::variant<std::uint64_t,ConcurrentFailure> VerifyConcurrentBoundary(
    const analysis::ValueProgram&,const CellPlan*,analysis::SemanticBudget = {});
}
#endif
