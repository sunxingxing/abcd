#ifndef ABCD_SOURCE_CONSTRUCTOR_FORWARDING_H_
#define ABCD_SOURCE_CONSTRUCTOR_FORWARDING_H_

#include "derived_projection.h"
#include "abcd/analysis/capture_effect.h"

namespace abcd::source {
struct ConstructorForwardingProof {
  RouteInputIdentity identity;
  abc::EntityOffset method;
  abc::FrameLayout frame;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  // Original instruction order; no static limit on mutually exclusive sites.
  std::vector<analysis::OpId> calls;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Explicit source projection only. Unlike whole-body implicit elision, effects
// and non-forwarding cycles remain in the body. Original frame writes are not
// visible in ordinary ValueSuperCall operands and must be proved separately.
[[nodiscard]] std::variant<ConstructorForwardingProof, DerivedProjectionError>
ProveConstructorForwarding(const FileFunction&, const FunctionContext&,
    analysis::SemanticBudget = {}, std::span<const analysis::CapturePreservation> = {});
[[nodiscard]] std::optional<DerivedProjectionError> VerifyConstructorForwarding(
    const FileFunction&, const FunctionContext&, const ConstructorForwardingProof&,
    analysis::SemanticBudget = {}, std::span<const analysis::CapturePreservation> = {});
}
#endif
