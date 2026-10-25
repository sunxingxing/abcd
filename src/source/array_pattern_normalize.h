#ifndef ABCD_SOURCE_ARRAY_PATTERN_NORMALIZE_H_
#define ABCD_SOURCE_ARRAY_PATTERN_NORMALIZE_H_

#include <variant>
#include <vector>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/analysis/semantic_ir.h"
#include "generator_delegate.h"

namespace abcd::source {
struct ArrayPatternBody {
  abc::MethodBody body;
  // Original PC of every kept instruction (parallel to body.instructions).
  std::vector<abc::CodeOffset> source_pcs;
  // Lifter certificates, sorted by PC; empty when nothing was folded.
  std::vector<analysis::PatternSite> sites;
};
// Fold each exact pinned es2panda array-pattern protocol (O0 register shape,
// docs/design/source-recovery/array-patterns.md) whose elements are holes or
// single register / STLEXVAR targets without defaults, rest or nesting. The
// acquisition, each Step and the context end each keep one placeholder
// instruction certified as an ArrayPatternOperation; all other protocol
// instructions and their four kinds of try region are removed. A pattern is
// folded only if no branch or handler outside it enters its interior and no
// removed register write is live at the pattern's normal or exceptional
// exits. Unmatched acquisitions are left untouched (for-of, defaults, rest...).
// `pcs` maps the input body to original PCs (empty: identity).
// O(I + H + P·R·(I+H)) for P folded patterns and R ≤ 12 protocol registers.
[[nodiscard]] std::variant<ArrayPatternBody, abc::MetadataError> NormalizeArrayPatterns(
    const abc::MethodBody& body, std::span<const abc::CodeOffset> pcs,
    const abc::FrameLayout& frame, const GeneratorNames& names, std::size_t bytes);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ARRAY_PATTERN_NORMALIZE_H_
