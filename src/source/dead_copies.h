#ifndef ABCD_SOURCE_DEAD_COPIES_H_
#define ABCD_SOURCE_DEAD_COPIES_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Exact original SSA uses, including handler phis. Only still-emitted private
// slot copies are removed; all effects and already-fused instructions remain
// roots. PC statements remain in place as zero-width anchors.
// O(V+C) with phased Reads, O(V+C log R) with normal Reads; C<=I removed
// copies, R native reads. O(I) admitted queue, exact counts are caller storage.
std::optional<SourceFailure> ElideDeadCopies(const AstMethod& method,
    SourceAst& ast, std::span<std::size_t> uses, std::size_t bytes);
}  // namespace abcd::source
#endif
