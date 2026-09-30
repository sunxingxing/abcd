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
  kOwner, kProtocol, kOrigin, kOperand, kResourceLimit, kWorkLimit
};
// Same verified input/work pair, with a genuine derived creation owner. Reprove
// original initialization and check actual working guard inputs before allowing
// a source consumer to omit them. This does not authorize heritage emission,
// receiver substitution, or changing constructor returns. Caller admits the
// retained input/work pair; original proof and projection use separate bounded
// passes with disjoint scratch and the supplied per-pass work limit.
[[nodiscard]] std::variant<DerivedProjection, DerivedProjectionError> ProjectDerivedConstructor(
    const FileFunction&, const FunctionContext&, analysis::SemanticBudget = {});
}
#endif
