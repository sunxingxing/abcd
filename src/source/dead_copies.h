#ifndef ABCD_SOURCE_DEAD_COPIES_H_
#define ABCD_SOURCE_DEAD_COPIES_H_

#include "semantic_ast_input.h"

namespace abcd::source {
class CopyOrigins;
// Original SSA uses, less explicitly certified consumed reads, including all
// handler phis. Only still-emitted private
// slot copies and primitive loads are removed; effects and fused nodes remain
// roots. PC statements remain in place as zero-width anchors.
// O(V+C) with phased Reads, O(V+C log R) with normal Reads; C<=I removed
// assignments, R native reads. O(I) admitted queue, counts are caller storage.
std::optional<SourceFailure> ElideDeadAssignments(const AstMethod& method,
    SourceAst& ast, std::span<std::size_t> uses, std::size_t bytes);
// Remove adjacent acc -> private register -> acc transport only when its SSA
// value has no other use (including handler phis). Original use counts remain
// conservative upper bounds for later folds. No allocation or value movement.
void ElideCopyReloads(const AstMethod& method, SourceAst& ast,
                     std::span<const std::size_t> uses);
// Reuse exact SSA copy identities within uninterrupted emitted slot-copy
// sequences. Keep live register saves, eliding only redundant acc reloads.
// No allocation; one pass with indexed SSA reads, no inter-block propagation.
void ElideKnownReloads(const AstMethod& method, SourceAst& ast,
                       const CopyOrigins& copies);
}  // namespace abcd::source
#endif
