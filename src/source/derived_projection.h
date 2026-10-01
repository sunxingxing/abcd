#ifndef ABCD_SOURCE_DERIVED_PROJECTION_H_
#define ABCD_SOURCE_DERIVED_PROJECTION_H_
#include "function_file.h"
namespace abcd::source {
struct DerivedProjection {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  analysis::OpId call;
  std::vector<analysis::OpId> guards;
};
enum class DerivedProjectionError : std::uint8_t {
  kOwner, kProtocol, kOrigin, kOperand, kResourceLimit, kWorkLimit, kRevision, kCoverage
};
// Same verified input/work pair, with a genuine derived creation owner. Reprove
// original initialization and check actual working guard inputs before allowing
// a source consumer to omit them. Range and spread share initialization; spread
// also requires the actual working callable operand to be this invocation's
// constructor. This does not authorize spread-array expansion, heritage emission,
// receiver substitution, or changing constructor returns. Caller admits the
// retained input/work pair; original proof and projection use separate bounded
// passes with disjoint scratch and the supplied per-pass work limit.
[[nodiscard]] std::variant<DerivedProjection, DerivedProjectionError> ProjectDerivedConstructor(
    const FileFunction&, const FunctionContext&, analysis::SemanticBudget = {});
// Recheck against the current same-owner body/working graph before consuming a
// cached projection. Revision equality alone is insufficient: IDs and complete
// guard coverage must match a fresh protocol proof.
[[nodiscard]] std::optional<DerivedProjectionError> VerifyDerivedProjection(
    const FileFunction&, const FunctionContext&, const DerivedProjection&,
    analysis::SemanticBudget = {});
}
#endif
