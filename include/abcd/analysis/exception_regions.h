#ifndef ABCD_ANALYSIS_EXCEPTION_REGIONS_H_
#define ABCD_ANALYSIS_EXCEPTION_REGIONS_H_

#include "abcd/analysis/exception_flow.h"

namespace abcd::analysis {
enum class CatchPart : std::uint8_t { kProtected, kHandler };
struct CatchScope {
  TryId region;
  CatchPart part;
};
struct InstructionRange {
  InstructionId begin;
  InstructionId end;  // Exclusive; may equal instruction count.
  bool operator==(const InstructionRange&) const = default;
};
// Original TryId and instruction ranges survive projection. The counts cover
// every protected instruction exactly once; all handler phases are unreachable.
struct InactiveExceptionRegion {
  TryId region;
  InstructionRange body, handler;
  std::uint32_t nonthrowing = 0, dispatched_elsewhere = 0, unreachable = 0;
  bool operator==(const InactiveExceptionRegion&) const = default;
};
struct CatchRegion {
  InstructionRange body;
  InstructionRange handler;
  HandlerId handler_id;
  std::optional<CatchScope> parent;
  std::optional<TryId> outer_dispatch;
  std::optional<InactiveExceptionRegion> inactive;
};
struct ExceptionRegions {
  // Indexed by encoded TryId, never reordered to change handler priority.
  std::vector<CatchRegion> regions;
  // Active regions in outer-before-inner source order; original TryIds remain stable.
  std::vector<TryId> source_order;
  std::vector<std::optional<CatchScope>> scopes;
  std::size_t allocated_bytes = 0;
  std::uint64_t work_units = 0;
};
enum class CatchRegionError : std::uint8_t {
  kBoundary, kHandlerShape, kCrossingScopes, kNormalEntry,
  kDispatchMismatch, kInvalidFlow, kResourceLimit
};
struct CatchRegionFailure {
  CatchRegionError kind;
  abc::CodeOffset pc;
  std::optional<TryId> region;
};

// Matching decoded body and unchanged phased flow. Proves disjoint/nested
// contiguous try/catch layout, normal entries and actual selected dispatch.
// Gaps between protected body and handler belong to the surrounding scope.
// No inferred finally, shared/typed handlers, dispatcher lowering or code
// motion. Normal exits still require the common region/AST branch lowering.
// This is a control-region certificate, not value or source admission.
// O(R log R + R log I + I + E + N) time; O(I+R) bounded additional storage.
[[nodiscard]] std::variant<ExceptionRegions, CatchRegionFailure>
BuildExceptionRegions(const abc::MethodBody& body, const ExceptionFlow& flow,
                      SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
