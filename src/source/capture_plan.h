#ifndef ABCD_SOURCE_CAPTURE_PLAN_H_
#define ABCD_SOURCE_CAPTURE_PLAN_H_

#include "scope_facts.h"

namespace abcd::source {
struct CaptureUse {
  analysis::CapturedSlot cell;
  analysis::CaptureDomain domain;
};
struct CaptureBinding {
  CaptureCell cell;
  analysis::CaptureDomain domain;
  // Set only for a Guarded use admitted before initialization: the owner's
  // first store of this local cell, which must become its TDZ declaration.
  std::optional<analysis::InstructionId> deferred = {};
};
struct CapturePlan {
  analysis::InstructionId site;
  abc::CodeOffset pc;
  abc::MethodPosition target;
  std::vector<CaptureBinding> bindings;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Retained transitive write-domain premise; a plan is not a family receipt.
  analysis::ClosureWrites writes = analysis::ClosureWrites::kUnknown;
};
enum class CaptureError : std::uint8_t {
  kCreation, kScope, kCoverage, kBinding, kResourceLimit, kWorkLimit
};
// A capture-plan refusal with its typed cause: kScope keeps the ScopeError of
// the rejected rebase. Compares equal to its kind so callers that only branch
// on the kind are unchanged; the cause is diagnostic and never a permission.
struct CaptureFailure {
  CaptureError kind;
  std::variant<std::monostate, ScopeError> cause;
  CaptureFailure(CaptureError value) : kind(value) {}  // NOLINT: implicit kind.
  CaptureFailure(CaptureError value, ScopeError scope) : kind(value), cause(scope) {}
  bool operator==(CaptureError value) const { return kind == value; }
};
// One genuine function/class capture point in the SAME owner as ScopeFacts.
// Function points use BindCreation; constructor/buffer points borrow their
// immutable FunctionContext. Uses are child-proven obligations, in occurrence
// order, not inferred from names.
// Outer CapturedSlot results retain their domain obligation. A local binding
// proves initialization/domain, NOT immutability, alias elimination or child
// emission. Keep the site even when two creations have the same target.
[[nodiscard]] std::variant<CapturePlan, CaptureFailure> BuildCapturePlan(
    const ScopeFacts& scopes, analysis::InstructionId site,
    const CaptureSite& creation, std::span<const CaptureUse> uses,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureFailure> VerifyCapturePlan(
    const ScopeFacts& scopes, analysis::InstructionId site,
    const CaptureSite& creation, std::span<const CaptureUse> uses,
    const CapturePlan& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
