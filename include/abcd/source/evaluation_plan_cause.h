#ifndef ABCD_SOURCE_EVALUATION_PLAN_CAUSE_H_
#define ABCD_SOURCE_EVALUATION_PLAN_CAUSE_H_
#include <cstdint>
#include <variant>
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
// Stable leaf domains shared by the subanalyses and their diagnostic envelope.
// Moving their declarations does not change enum values or admission.
enum class HoleProtocolError : std::uint8_t {
  kInput,
  kEscape,
  kUnguardedThrow,
  kResourceLimit,
  kWorkLimit
};
enum class CellEvaluationError : std::uint8_t {
  kEnvironment, kCells, kInitialization, kInternalIdentity, kResourceLimit, kWorkLimit
};
enum class LiteralError : std::uint8_t {
  kRevision, kFlow, kDefinition, kReceiver, kObserved, kLateValue, kDomain,
  kCoverage, kResourceLimit, kWorkLimit, kNameSettingProof
};
enum class CallSpreadError : std::uint8_t {
  kConstruction, kEscape, kCursor, kElementDomain, kCallee, kReceiver,
  kOrder, kCatchState, kCorrespondence, kPremise
};
enum class EvaluationShapeError : std::uint8_t { kMissingShape };
enum class EvaluationInputCheck : std::uint8_t {
  kHoleTransport, kHoleUse, kCellIdentity, kLiteralAllocation,
  kLiteralRhsAvailability, kLiteralOrder, kLiteralNesting, kLiteralUse, kLiteralStagedStorage, kLiteralStagedAcyclic, kWideRangePremise
};
struct EvaluationInputSite {
  std::optional<EvaluationInputCheck> check;
  std::optional<analysis::OpId> operation;
  std::optional<analysis::UseId> use;
  std::optional<analysis::ValueId> value;
  bool operator==(const EvaluationInputSite&) const = default;
};
using EvaluationPlanCause = std::variant<std::monostate, HoleProtocolError,
    CellEvaluationError, LiteralError, EvaluationShapeError, CallSpreadError>;
} // namespace abcd::source
#endif
