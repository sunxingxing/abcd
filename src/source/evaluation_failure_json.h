#ifndef ABCD_SOURCE_EVALUATION_FAILURE_JSON_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_JSON_H_
#include "abcd/source/evaluation_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json EvaluationFailureJson(const EvaluationEvidence& v) {
  nlohmann::json j{{"obligation", static_cast<unsigned>(v.obligation)}, {"location", TreeLocationJson(v.location)}};
  if (v.for_in) {
    const auto& f = *v.for_in;
    nlohmann::json item{{"kind", static_cast<unsigned>(f.kind)}};
    if (f.acquisition) item["acquisition"] = f.acquisition->value;
    if (f.operation) item["operation"] = f.operation->value;
    if (f.value) item["value"] = f.value->value;
    if (f.use) item["use"] = f.use->value;
    if (f.block) item["block"] = f.block->value;
    if (f.edge) item["edge"] = f.edge->value;
    j["for_in"] = std::move(item);
  }
  if (v.use) j["use"] = v.use->value;
  if (v.value) j["value"] = v.value->value;
  if (v.selection_exit) j["selection_exit"] = static_cast<unsigned>(*v.selection_exit);
  if (v.selection_edge) j["selection_edge"] = v.selection_edge->value;
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
  if (j.contains("for_in")) {
    const auto& f = j["for_in"];
    if (!f.is_object() || !FailureNumber(f,"kind",n) || n > static_cast<unsigned>(ForInError::kWorkLimit)) return {};
    ForInFailure failure{static_cast<ForInError>(n)};
    if (!FailureOptionalId(f,"acquisition",failure.acquisition) || !FailureOptionalId(f,"operation",failure.operation) ||
        !FailureOptionalId(f,"value",failure.value) || !FailureOptionalId(f,"use",failure.use) ||
        !FailureOptionalId(f,"block",failure.block) || !FailureOptionalId(f,"edge",failure.edge)) return {};
    v.for_in = failure;
  }
  if (!FailureOptionalId(j,"use",v.use) || !FailureOptionalId(j,"value",v.value) ||
      !FailureOptionalId(j,"selection_edge",v.selection_edge) || !FailureOptionalId(j,"expected_consumer",v.expected_consumer) || !FailureOptionalId(j,"actual_consumer",v.actual_consumer)) return {};
  if (j.contains("selection_exit")) { if (!FailureNumber(j,"selection_exit",n) || n > static_cast<unsigned>(SelectionExitReason::kKeepEdge)) return {}; v.selection_exit = static_cast<SelectionExitReason>(n); }
  if (j.contains("placement")) { if (!FailureNumber(j,"placement",n) || n > static_cast<unsigned>(EvaluationKind::kForInEvent)) return {}; v.placement = static_cast<EvaluationKind>(n); }
  if (j.contains("temporary")) { if (!FailureNumber(j,"temporary",n) || n > static_cast<unsigned>(TemporaryReason::kEnumerationKey)) return {}; v.temporary = static_cast<TemporaryReason>(n); }
  if (j.contains("expected_temporary")) { if (!FailureNumber(j,"expected_temporary",n) || n > static_cast<unsigned>(TemporaryReason::kEnumerationKey)) return {}; v.expected_temporary = static_cast<TemporaryReason>(n); }
  if (j.contains("expected_owner")) { v.expected_owner = ParseTreeLocation(j["expected_owner"]); if (!v.expected_owner) return {}; }
  if (j.contains("actual_owner")) { v.actual_owner = ParseTreeLocation(j["actual_owner"]); if (!v.actual_owner) return {}; }
  return v;
}
} // namespace abcd::source
#endif
