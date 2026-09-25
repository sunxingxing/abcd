#ifndef ABCD_SOURCE_ARGUMENT_REGION_H_
#define ABCD_SOURCE_ARGUMENT_REGION_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct AstMethod;
// The property node is emitted by the outer owner; its arguments and call are
// emitted recursively. Block-end positions use the same exclusive boundary.
inline bool IsArgumentInterior(const SourceAst& ast, std::size_t instruction) {
  return ast.argument_region && instruction > ast.argument_region->property.value &&
         instruction <= ast.argument_region->call.value;
}
inline bool HasArgumentOverlap(const SourceAst& ast, std::size_t first,
                               std::size_t end) {
  return ast.argument_region && ast.argument_region->property.value < end &&
         ast.argument_region->call.value >= first;
}
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
// The conditional assignment and every header/join statement must have
// expression grammar. Keep all private commits and the live call Reference.
bool CanSequenceArgumentRegion(const SourceAst& ast,
    const analysis::NormalFlow& flow, const analysis::RegionIf& region);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ARGUMENT_REGION_H_
