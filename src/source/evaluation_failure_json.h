#ifndef ABCD_SOURCE_EVALUATION_FAILURE_JSON_H_
#define ABCD_SOURCE_EVALUATION_FAILURE_JSON_H_
#include "abcd/source/evaluation_failure.h"
#include "tree_failure_json.h"
#include "concurrent_failure_json.h"
namespace abcd::source {
inline nlohmann::json EvaluationFailureJson(const EvaluationEvidence& v) {
  nlohmann::json j{{"obligation", static_cast<unsigned>(v.obligation)}, {"location", TreeLocationJson(v.location)}};
  std::visit([&](const auto& cause) {
    using T = std::decay_t<decltype(cause)>;
    if constexpr (!std::is_same_v<T, std::monostate>) {
      const char* domain = nullptr;
      if constexpr (std::is_same_v<T, HoleProtocolError>) domain = "HoleProtocolError";
      else if constexpr (std::is_same_v<T, CellEvaluationError>) domain = "CellEvaluationError";
      else if constexpr (std::is_same_v<T, LiteralError>) domain = "LiteralError";
      else if constexpr (std::is_same_v<T, CallSpreadError>) domain = "CallSpreadError";
      else if constexpr (std::is_same_v<T, EvaluationShapeError>) domain = "EvaluationShapeError";
      j["plan_cause"] = {{"domain", domain}, {"value", static_cast<unsigned>(cause)}};
    }
  }, v.plan_cause);
  if (v.protocol_context) j["protocol_context"] = static_cast<unsigned>(*v.protocol_context);
  if(v.concurrent)j["concurrent"]=ConcurrentJson(*v.concurrent);
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
  if (v.snapshot_candidate) {
    const auto& c = *v.snapshot_candidate;
    nlohmann::json item{{"reason", static_cast<unsigned>(c.reason)}};
    if (c.occurrence) item["occurrence"] = c.occurrence->value;
    if (c.producer) item["producer"] = static_cast<unsigned>(*c.producer);
    if (c.consumer) item["consumer"] = static_cast<unsigned>(*c.consumer);
    j["snapshot_candidate"] = std::move(item);
  }
  if (v.logical_coverage) {
    const auto& c = *v.logical_coverage;
    nlohmann::json item{{"check", static_cast<unsigned>(c.check)}, {"registered", c.registered}, {"covered", c.covered}};
    if (c.branch) item["branch"] = c.branch->value;
    if (c.route_entry) item["route_entry"] = c.route_entry->value;
    if (c.route_exit) item["route_exit"] = c.route_exit->value;
    j["logical_coverage"] = std::move(item);
  }
  if (v.input_check) j["input_check"] = static_cast<unsigned>(*v.input_check);
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
  if (!j.is_object() || !FailureNumber(j,"obligation",n) || n > static_cast<unsigned>(EvaluationObligation::kProtocolContext) || !j.contains("location")) return {};
  EvaluationEvidence v; v.obligation = static_cast<EvaluationObligation>(n);
  auto location = ParseTreeLocation(j["location"]); if (!location) return {}; v.location = *location;
  if (j.contains("plan_cause")) {
    const auto& c = j["plan_cause"];
    if (!c.is_object() || !c.contains("domain") || !c["domain"].is_string() || !FailureNumber(c,"value",n)) return {};
    const auto domain = c["domain"].get<std::string>();
    if (domain == "HoleProtocolError" && n <= static_cast<unsigned>(HoleProtocolError::kWorkLimit)) v.plan_cause = static_cast<HoleProtocolError>(n);
    else if (domain == "CellEvaluationError" && n <= static_cast<unsigned>(CellEvaluationError::kWorkLimit)) v.plan_cause = static_cast<CellEvaluationError>(n);
    else if (domain == "LiteralError" && n <= static_cast<unsigned>(LiteralError::kNameSettingProof)) v.plan_cause = static_cast<LiteralError>(n);
    else if (domain == "CallSpreadError" && n <= static_cast<unsigned>(CallSpreadError::kPremise)) v.plan_cause = static_cast<CallSpreadError>(n);
    else if (domain == "EvaluationShapeError" && n <= static_cast<unsigned>(EvaluationShapeError::kMissingShape)) v.plan_cause = static_cast<EvaluationShapeError>(n);
    else return {};
  }
  if (j.contains("protocol_context")) {
    if (!FailureNumber(j,"protocol_context",n) || n > static_cast<unsigned>(ProtocolContextCheck::kMissingPlan)) return {};
    v.protocol_context = static_cast<ProtocolContextCheck>(n);
  }
  if(j.contains("concurrent")){v.concurrent=ParseConcurrent(j["concurrent"]);if(!v.concurrent)return {};}
  if (j.contains("for_in")) {
    const auto& f = j["for_in"];
    if (!f.is_object() || !FailureNumber(f,"kind",n) || n > static_cast<unsigned>(ForInError::kWorkLimit)) return {};
    ForInFailure failure{static_cast<ForInError>(n)};
    if (!FailureOptionalId(f,"acquisition",failure.acquisition) || !FailureOptionalId(f,"operation",failure.operation) ||
        !FailureOptionalId(f,"value",failure.value) || !FailureOptionalId(f,"use",failure.use) ||
        !FailureOptionalId(f,"block",failure.block) || !FailureOptionalId(f,"edge",failure.edge)) return {};
    v.for_in = failure;
  }
  if (j.contains("snapshot_candidate")) {
    const auto& c = j["snapshot_candidate"]; SnapshotCandidateEvidence evidence;
    if (!c.is_object() || !FailureNumber(c,"reason",n) || n > static_cast<unsigned>(SnapshotCandidateReason::kConsumerPlacement)) return {};
    evidence.reason = static_cast<SnapshotCandidateReason>(n);
    if (!FailureOptionalId(c,"occurrence",evidence.occurrence)) return {};
    if (c.contains("producer")) { if (!FailureNumber(c,"producer",n) || n > static_cast<unsigned>(EvaluationKind::kTemplateConstruction)) return {}; evidence.producer = static_cast<EvaluationKind>(n); }
    if (c.contains("consumer")) { if (!FailureNumber(c,"consumer",n) || n > static_cast<unsigned>(EvaluationKind::kTemplateConstruction)) return {}; evidence.consumer = static_cast<EvaluationKind>(n); }
    v.snapshot_candidate = evidence;
  }
  if (j.contains("logical_coverage")) {
    const auto& c = j["logical_coverage"]; LogicalCoverageEvidence evidence;
    if (!c.is_object() || !FailureNumber(c,"check",n) || n > static_cast<unsigned>(LogicalCoverageCheck::kConsumedEvent) ||
        !c.contains("registered") || !c["registered"].is_boolean() ||
        !c.contains("covered") || !c["covered"].is_boolean()) return {};
    evidence.check = static_cast<LogicalCoverageCheck>(n);
    evidence.registered = c["registered"].get<bool>(); evidence.covered = c["covered"].get<bool>();
    if (!FailureOptionalId(c,"branch",evidence.branch) || !FailureOptionalId(c,"route_entry",evidence.route_entry) ||
        !FailureOptionalId(c,"route_exit",evidence.route_exit)) return {};
    v.logical_coverage = evidence;
  }
  if (j.contains("input_check")) { if (!FailureNumber(j,"input_check",n) || n > static_cast<unsigned>(EvaluationInputCheck::kWideRangePremise)) return {}; v.input_check = static_cast<EvaluationInputCheck>(n); }
  if (!FailureOptionalId(j,"use",v.use) || !FailureOptionalId(j,"value",v.value) ||
      !FailureOptionalId(j,"selection_edge",v.selection_edge) || !FailureOptionalId(j,"expected_consumer",v.expected_consumer) || !FailureOptionalId(j,"actual_consumer",v.actual_consumer)) return {};
  if (j.contains("selection_exit")) { if (!FailureNumber(j,"selection_exit",n) || n > static_cast<unsigned>(SelectionExitReason::kKeepEdge)) return {}; v.selection_exit = static_cast<SelectionExitReason>(n); }
  if (j.contains("placement")) { if (!FailureNumber(j,"placement",n) || n > static_cast<unsigned>(EvaluationKind::kTemplateConstruction)) return {}; v.placement = static_cast<EvaluationKind>(n); }
  if (j.contains("temporary")) { if (!FailureNumber(j,"temporary",n) || n > static_cast<unsigned>(TemporaryReason::kEnumerationKey)) return {}; v.temporary = static_cast<TemporaryReason>(n); }
  if (j.contains("expected_temporary")) { if (!FailureNumber(j,"expected_temporary",n) || n > static_cast<unsigned>(TemporaryReason::kEnumerationKey)) return {}; v.expected_temporary = static_cast<TemporaryReason>(n); }
  if (j.contains("expected_owner")) { v.expected_owner = ParseTreeLocation(j["expected_owner"]); if (!v.expected_owner) return {}; }
  if (j.contains("actual_owner")) { v.actual_owner = ParseTreeLocation(j["actual_owner"]); if (!v.actual_owner) return {}; }
  return v;
}
} // namespace abcd::source
#endif
