#ifndef ABCD_SOURCE_DEFAULT_SIGNATURE_H_
#define ABCD_SOURCE_DEFAULT_SIGNATURE_H_

#include "abcd/source/source_tree.h"
#include "default_body.h"
#include "evaluation_plan.h"

namespace abcd::source {
struct ControlPlan;
struct DefaultCaptureContext { const ControlPlan& control; const EvaluationPlan& evaluation; };
enum class DefaultSignatureError : std::uint8_t {
  kRevision, kContext, kParameter, kLiteral, kPlacement, kOrigin, kStructure,
  kResourceLimit, kWorkLimit
};
struct DefaultSignatureProof {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::size_t allocated_bytes;
  std::uint64_t work;
};
// Both calls count tree storage and owned scratch. Body storage belongs to the
// caller. Neither report alone proves residual body semantics or callable ABI.
// Build mutates construction-only syntax; on failure the caller must discard it.
[[nodiscard]] std::variant<DefaultSignatureProof, DefaultSignatureError>
BuildDefaultSignature(const DefaultBody& body, FunctionTree& tree,
                      analysis::SemanticBudget budget = {}, const DefaultCaptureContext* capture = nullptr);
// Independently checks actual initializer nodes/placements against the sealed
// body owner, without regenerating syntax or accepting a builder certificate.
[[nodiscard]] std::variant<DefaultSignatureProof, DefaultSignatureError>
VerifyDefaultSignature(const DefaultBody& body, const FunctionTree& tree,
                       analysis::SemanticBudget budget = {}, const DefaultCaptureContext* capture = nullptr);
}  // namespace abcd::source
#endif
