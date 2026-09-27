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
// One BindCreation result in the SAME verified owner as ScopeFacts. Uses are
// child-proven obligations, in occurrence order, not inferred from names.
// Outer CapturedSlot results retain their domain obligation. A local binding
// proves initialization/domain, NOT immutability, alias elimination or child
// emission. Keep the site even when two creations have the same target.
[[nodiscard]] std::variant<CapturePlan, CaptureError> BuildCapturePlan(
    const ScopeFacts& scopes, analysis::InstructionId site,
    const analysis::CreationBinding& creation, std::span<const CaptureUse> uses,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CaptureError> VerifyCapturePlan(
    const ScopeFacts& scopes, analysis::InstructionId site,
    const analysis::CreationBinding& creation, std::span<const CaptureUse> uses,
    const CapturePlan& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
