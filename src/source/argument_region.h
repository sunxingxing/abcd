#ifndef ABCD_SOURCE_ARGUMENT_REGION_H_
#define ABCD_SOURCE_ARGUMENT_REGION_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct AstMethod;
// One cross-block interval per method for now: one O(B+E) validation, never
// an entire CFG scan per call. All normal paths must converge at the call;
// the outer SSA proof separately owns getter/receiver/callee identities.
std::optional<SourceArgumentRegion> ProveArgumentRegion(
    const AstMethod& method, std::size_t property,
    std::size_t call);
// Refine the proven forward interval using its CFG, never printed source.
// A contiguous two-arm diamond can use the normal if writer inside the IIFE.
std::optional<analysis::RegionIf> ProveArgumentIf(
    const SourceAst& ast, const analysis::NormalFlow& flow);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ARGUMENT_REGION_H_
