#ifndef ABCD_SOURCE_CAPTURE_TREE_H_
#define ABCD_SOURCE_CAPTURE_TREE_H_

#include "capture_cells.h"
#include "tree_flow.h"

namespace abcd::source {
class FunctionContext;
// Borrowed, unchanged products of the common structure/flow/operand checks.
// Revisions are necessary, not a substitute for those checks. In particular
// ordinary FunctionExpr structure proves visibility at the creation site.
// A class layout alone does not: class-root aliases remain conditional until
// enclosing ClassExpr scope references and family write checks are connected.
struct CaptureTree {
  const FunctionTree& tree;
  const TreeFlow& flow;
};
// A genuine class-root occurrence projected into this actual ClassExpr.
// Borrowed for synchronous construction only; original cell binding and the
// eventual enclosing-scope/write checks remain independent obligations.
struct ClassCaptureAnchor {
  analysis::OpId creation;
  const FunctionContext& owner;
};
struct ClassCaptureParent {
  CaptureTree tree;
  analysis::OpId creation;
};
enum class CaptureTreeError : std::uint8_t {
  kOwner, kRevision, kCreation, kCoverage, kCell, kTarget, kName,
  kOuterCapture, kWorkLimit, kResourceLimit
};
struct IncomingCaptureTree {
  // Borrowed only during synchronous construction; the child owns copied names
  // and typed symbol targets, never references to these parent arenas.
  const CaptureCells& cells;
  CaptureTree parent;
  std::optional<ClassCaptureAnchor> class_anchor;
};
[[nodiscard]] std::variant<std::uint64_t, CaptureTreeError> VerifyClassCaptureAnchor(
    const CaptureCells& cells, CaptureTree parent, const ClassCaptureAnchor& anchor,
    analysis::SemanticBudget budget = {});
// Connects a previously verified CaptureCells proof to actual parent/child
// syntax. No tree reconstruction or symbol-number agreement is assumed.
// O(captures + compared UTF-16 units), constant scratch. Caller admits both
// trees/flows and capture proof. This does not certify callable metadata,
// strictness, free globals, transitive writes or complete family ownership.
[[nodiscard]] std::variant<std::uint64_t, CaptureTreeError> VerifyCaptureTree(
    const CaptureCells& cells, CaptureTree parent, CaptureTree child,
    analysis::SemanticBudget budget = {}, const ClassCaptureAnchor* class_anchor = nullptr);
}  // namespace abcd::source
#endif
