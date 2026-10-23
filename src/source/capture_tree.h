#ifndef ABCD_SOURCE_CAPTURE_TREE_H_
#define ABCD_SOURCE_CAPTURE_TREE_H_

#include "capture_cells.h"
#include "tree_flow.h"

namespace abcd::source {
class FunctionContext;
// Borrowed, unchanged products of the common structure/flow/operand checks.
// Revisions are necessary, not a substitute for those checks. In particular
// ordinary FunctionExpr structure proves visibility at the creation site.
// A class layout alone does not: final enclosing ClassExpr structure checks
// must resolve the composed roots' captures, and family writes stay independent.
struct CaptureTree {
  const FunctionTree& tree;
  const TreeFlow& flow;
};
// The FunctionExpr creating child at a creation operation's own expression:
// that expression itself, or one method property of its object literal (the
// literal evaluates its methods at the same position). Adds the properties
// examined to *examined; null if no such single expression exists.
[[nodiscard]] inline const FunctionExpr* CreationFunctionExpr(const FunctionTree& tree,
    ExprId expression, FunctionOwnerId child, std::uint64_t* examined) {
  if (expression.value >= tree.expressions.size()) return nullptr;
  const auto& node = tree.expressions[expression.value].expression;
  if (const auto* function = std::get_if<FunctionExpr>(&node))
    return function->child == child ? function : nullptr;
  const auto* object = std::get_if<ObjectExpr>(&node);
  if (!object || object->kind != AggregateKind::kObject ||
      object->properties.first > tree.properties.size() ||
      object->properties.count > tree.properties.size() - object->properties.first) return nullptr;
  const FunctionExpr* found = nullptr;
  for (std::uint32_t p = 0; p < object->properties.count; ++p) {
    ++*examined;
    const auto& property = tree.properties[object->properties.first + p];
    if (!property.method || property.value.value >= tree.expressions.size()) continue;
    const auto* function = std::get_if<FunctionExpr>(&tree.expressions[property.value.value].expression);
    if (!function || function->child != child) continue;
    if (found) return nullptr;
    found = function;
  }
  return found;
}
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
// Borrow the unchanged final parent whose structure traversal checked the
// composed ClassExpr's lexical references. Revisions/names/occurrence are
// rechecked per root; visibility is checked once in the parent traversal,
// never by replaying the whole parent for each member. No printer CFG inference.
struct ClassCaptureDestination {
  const FunctionTree& tree;
  ExprId expression;
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
  // Sealed occurrence, borrowed spelling: consume before the child/parent
  // capture check, so nested closures never alias a projected source cache.
  std::shared_ptr<const ClassInnerScopeProof> class_inner;
  std::u16string_view class_name;
};
// A checked creator flow, not an allow flag: each requested store must own the
// real lexical declaration. Does not grant an activation proof to recovery.
[[nodiscard]] bool HasDeferredDeclaration(CaptureTree, CellId);
[[nodiscard]] std::variant<std::uint64_t, CaptureTreeError> VerifyDeferredCaptureTree(
    const CaptureCells&, CaptureTree, analysis::SemanticBudget = {}, std::optional<std::size_t>* rejected = nullptr);
[[nodiscard]] std::variant<std::uint64_t, CaptureTreeError> VerifyClassCaptureAnchor(
    const CaptureCells& cells, CaptureTree parent, const ClassCaptureAnchor& anchor,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureTreeError> VerifyClassCaptureDestination(
    const CaptureCells&, const FunctionTree& child, ClassCaptureDestination,
    const FunctionContext& owner, analysis::SemanticBudget budget = {});
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
