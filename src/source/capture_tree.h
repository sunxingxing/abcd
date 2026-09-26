#ifndef ABCD_SOURCE_CAPTURE_TREE_H_
#define ABCD_SOURCE_CAPTURE_TREE_H_

#include "capture_cells.h"
#include "tree_flow.h"

namespace abcd::source {
// Borrowed, unchanged products of the common structure/flow/operand checks.
// Revisions are necessary, not a substitute for those checks. In particular
// parent structure verification proves visibility at the actual creation site.
struct CaptureTree {
  const FunctionTree& tree;
  const TreeFlow& flow;
};
enum class CaptureTreeError : std::uint8_t {
  kOwner, kRevision, kCreation, kCoverage, kCell, kTarget, kName,
  kOuterCapture, kWorkLimit
};
// Connects a previously verified CaptureCells proof to actual parent/child
// syntax. No tree reconstruction or symbol-number agreement is assumed.
// O(captures + compared UTF-16 units), constant scratch. Caller admits both
// trees/flows and capture proof. This does not certify callable metadata,
// strictness, free globals, transitive writes or complete family ownership.
[[nodiscard]] std::variant<std::uint64_t, CaptureTreeError> VerifyCaptureTree(
    const CaptureCells& cells, CaptureTree parent, CaptureTree child,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
