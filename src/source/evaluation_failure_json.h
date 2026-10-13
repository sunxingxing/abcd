#ifndef ABCD_SOURCE_EVALUATION_FAILURE_JSON_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_JSON_H_
#include "abcd/source/evaluation_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json EvaluationFailureJson(const EvaluationEvidence& v) {
  nlohmann::json j{{"obligation", static_cast<unsigned>(v.obligation)}, {"location", TreeLocationJson(v.location)}};
  if (v.use) j["use"] = v.use->value;
  if (v.value) j["value"] = v.value->value;
  if (v.expected_consumer) j["expected_consumer"] = v.expected_consumer->value;
  if (v.actual_consumer) j["actual_consumer"] = v.actual_consumer->value;
  if (v.placement) j["placement"] = static_cast<unsigned>(*v.placement);
  if (v.expected_temporary) j["expected_temporary"] = static_cast<unsigned>(*v.expected_temporary);
  if (v.temporary) j["temporary"] = static_cast<unsigned>(*v.temporary);
  if (v.expected_owner) j["expected_owner"] = TreeLocationJson(*v.expected_owner);
  if (v.actual_owner) j["actual_owner"] = TreeLocationJson(*v.actual_owner);
  return j;
}
inline std::optional<EvaluationEvidence> ParseEvaluationFailure(const nlohmann::json& j) {
  std::uint32_t n;
  if (!j.is_object() || !FailureNumber(j,"obligation",n) || n > static_cast<unsigned>(EvaluationObligation::kUse) || !j.contains("location")) return {};
  EvaluationEvidence v; v.obligation = static_cast<EvaluationObligation>(n);
  auto location = ParseTreeLocation(j["location"]); if (!location) return {}; v.location = *location;
  if (!FailureOptionalId(j,"use",v.use) || !FailureOptionalId(j,"value",v.value) ||
      !FailureOptionalId(j,"expected_consumer",v.expected_consumer) || !FailureOptionalId(j,"actual_consumer",v.actual_consumer)) return {};
  if (j.contains("placement")) { if (!FailureNumber(j,"placement",n) || n > static_cast<unsigned>(EvaluationKind::kGuardedCellRead)) return {}; v.placement = static_cast<EvaluationKind>(n); }
  if (j.contains("temporary")) { if (!FailureNumber(j,"temporary",n) || n > static_cast<unsigned>(TemporaryReason::kDecisionPredicate)) return {}; v.temporary = static_cast<TemporaryReason>(n); }
  if (j.contains("expected_temporary")) { if (!FailureNumber(j,"expected_temporary",n) || n > static_cast<unsigned>(TemporaryReason::kDecisionPredicate)) return {}; v.expected_temporary = static_cast<TemporaryReason>(n); }
  if (j.contains("expected_owner")) { v.expected_owner = ParseTreeLocation(j["expected_owner"]); if (!v.expected_owner) return {}; }
  if (j.contains("actual_owner")) { v.actual_owner = ParseTreeLocation(j["actual_owner"]); if (!v.actual_owner) return {}; }
  return v;
}
} // namespace abcd::source
#endif
