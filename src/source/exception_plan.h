#ifndef ABCD_SOURCE_EXCEPTION_PLAN_H_
#define ABCD_SOURCE_EXCEPTION_PLAN_H_

#include "abcd/analysis/exception_regions.h"
#include "abcd/source/exception_region_failure.h"
#include "../analysis/phased_value_import.h"

namespace abcd::source {
struct CatchBinding {
  // Declared in this region's handler scope, even when a nested protected
  // scope starts at the same handler PC. It is not a function-body local.
  analysis::WorkBlockId block;
  analysis::ValueId value;
  bool operator==(const CatchBinding&) const = default;
};
enum class ExceptionRegionRole : std::uint8_t { kOrdinaryTry, kInactive, kProtocolOwned, kForwardedTry };
struct ExceptionRegion {
  analysis::WorkBlockId body;
  analysis::WorkBlockId handler;
  std::optional<CatchBinding> binding;
  bool operator==(const ExceptionRegion&) const = default;
};
struct NormalTransfer { bool operator==(const NormalTransfer&) const = default; };
struct RaiseTransfer {
  analysis::TryId region;
  bool operator==(const RaiseTransfer&) const = default;
};
struct CatchTransfer {
  analysis::TryId region;
  bool operator==(const CatchTransfer&) const = default;
};
using ExceptionTransfer = std::variant<NormalTransfer, RaiseTransfer, CatchTransfer>;
struct ExceptionPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  // Encoded TryId order. Lexical order and instruction scopes stay in layout.
  analysis::ExceptionRegions layout;
  std::vector<ExceptionRegion> regions;
  // EdgeId indexed: raising edges are not normal after-operation copy sites.
  std::vector<ExceptionTransfer> transfers;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ExceptionPlanError : std::uint8_t {
  kInput, kRevision, kLayout, kBinding, kTransfer, kWorkLimit, kResourceLimit
};
struct ExceptionPlanFailure {
  ExceptionPlanError kind;
  analysis::CatchRegionFailure cause;
  std::optional<ExceptionRegionEvidence> exception_region;
};
// Matching original body/phase analyses only. This plan preserves catch payload
// identity and dispatch; it is not yet a control tree or executable-source proof.
// Additional storage O(I+B+E+R), indexing work O(I+B+V+E+R); total work
// additionally includes the existing phase-availability and region checks.
// All caller-owned inputs (including the plan for verification) are admitted
// separately. The grant covers simultaneous additional results and scratch.
[[nodiscard]] std::variant<ExceptionPlan, ExceptionPlanError, ExceptionPlanFailure> BuildExceptionPlan(
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, ExceptionPlanError> VerifyExceptionPlan(
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    const analysis::PhasedValueInput& input, const ExceptionPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
