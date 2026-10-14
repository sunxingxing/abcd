#ifndef ABCD_SOURCE_TREE_FAILURE_JSON_H_
#define ABCD_SOURCE_TREE_FAILURE_JSON_H_
#include "abcd/source/tree_failure.h"
#include <nlohmann/json.hpp>
#include <limits>
namespace abcd::source {
inline nlohmann::json TreeLocationJson(const TreeLocation& v) {
  nlohmann::json j = nlohmann::json::object();
  if (v.method) j["method"] = v.method->value;
  if (v.operation) j["operation"] = v.operation->value;
  if (v.block) j["block"] = v.block->value;
  if (v.control) j["control"] = v.control->value;
  if (v.edge) j["edge"] = v.edge->value;
  if (v.expression) j["expression"] = v.expression->value;
  if (v.statement) j["statement"] = v.statement->value;
  if (v.pc) j["pc"] = v.pc->value;
  if (v.working_pc) j["working_pc"] = v.working_pc->value;
  if (v.synthetic) j["synthetic"] = {{"kind", static_cast<unsigned>(v.synthetic->kind)}, {"source", v.synthetic->source}};
  return j;
}
inline bool FailureNumber(const nlohmann::json& j, const char* name, std::uint32_t& out) {
  const auto it = j.find(name);
  if (it == j.end() || !it->is_number_unsigned() || it->get<std::uint64_t>() > UINT32_MAX) return false;
  out = it->get<std::uint32_t>(); return true;
}
template<class Id> bool FailureOptionalId(const nlohmann::json& j, const char* name, std::optional<Id>& out) {
  if (!j.contains(name)) return true;
  std::uint32_t v; if (!FailureNumber(j, name, v)) return false;
  out = Id{v}; return true;
}
inline std::optional<TreeLocation> ParseTreeLocation(const nlohmann::json& j) {
  if (!j.is_object()) return {};
  TreeLocation v;
  if (!FailureOptionalId(j, "method", v.method)) return {};
  if (!FailureOptionalId(j, "operation", v.operation)) return {};
  if (!FailureOptionalId(j, "block", v.block)) return {};
  if (!FailureOptionalId(j, "control", v.control)) return {};
  if (!FailureOptionalId(j, "edge", v.edge)) return {};
  if (!FailureOptionalId(j, "expression", v.expression)) return {};
  if (!FailureOptionalId(j, "statement", v.statement)) return {};
  if (!FailureOptionalId(j, "pc", v.pc)) return {};
  if (!FailureOptionalId(j, "working_pc", v.working_pc)) return {};
  if (j.contains("synthetic")) {
    std::uint32_t kind, source;
    if (!FailureNumber(j["synthetic"], "kind", kind) || !FailureNumber(j["synthetic"], "source", source) ||
        kind < static_cast<unsigned>(ControlOriginKind::kTry) || kind > static_cast<unsigned>(ControlOriginKind::kLoopTransfer) ||
        v.pc || v.working_pc || v.operation || v.block) return {};
    v.synthetic = TreeSyntheticLocation{static_cast<ControlOriginKind>(kind), source};
  }
  return v;
}
inline nlohmann::json TreeFailureJson(const TreeFailureEvidence& v) {
  nlohmann::json j{{"obligation", static_cast<unsigned>(v.obligation)}, {"location", TreeLocationJson(v.location)}};
  if (v.expected_owner) j["expected_owner"] = TreeLocationJson(*v.expected_owner);
  if (v.actual_owner) j["actual_owner"] = TreeLocationJson(*v.actual_owner);
  if (v.ownership) j["ownership"] = static_cast<unsigned>(*v.ownership);
  if (v.placement) j["placement"] = static_cast<unsigned>(*v.placement);
  if (v.decision_entry) j["decision_entry"] = TreeLocationJson(*v.decision_entry);
  if (v.decision_prefix) j["decision_prefix"] = static_cast<unsigned>(*v.decision_prefix);
  if (v.prefix_event) j["prefix_event"] = TreeLocationJson(*v.prefix_event);
  return j;
}
inline std::optional<TreeFailureEvidence> ParseTreeFailure(const nlohmann::json& j) {
  std::uint32_t kind;
  if (!j.is_object() || !FailureNumber(j, "obligation", kind) || kind > static_cast<unsigned>(TreeObligation::kForInRecovery) || !j.contains("location")) return {};
  auto location = ParseTreeLocation(j["location"]); if (!location) return {};
  TreeFailureEvidence v{static_cast<TreeObligation>(kind), *location};
  if (j.contains("expected_owner")) { v.expected_owner = ParseTreeLocation(j["expected_owner"]); if (!v.expected_owner) return {}; }
  if (j.contains("actual_owner")) { v.actual_owner = ParseTreeLocation(j["actual_owner"]); if (!v.actual_owner) return {}; }
  if (j.contains("ownership")) { std::uint32_t n; if (!FailureNumber(j,"ownership",n) || n > static_cast<unsigned>(ExpressionOwnershipError::kPlacement)) return {}; v.ownership = static_cast<ExpressionOwnershipError>(n); }
  if (j.contains("placement")) { std::uint32_t n; if (!FailureNumber(j,"placement",n) || n > static_cast<unsigned>(EvaluationKind::kForInEvent)) return {}; v.placement = static_cast<EvaluationKind>(n); }
  if (j.contains("decision_entry")) { v.decision_entry = ParseTreeLocation(j["decision_entry"]); if (!v.decision_entry) return {}; }
  if (j.contains("decision_prefix")) { std::uint32_t n; if (!FailureNumber(j,"decision_prefix",n) || n > static_cast<unsigned>(DecisionPrefixError::kStatementExpression)) return {}; v.decision_prefix = static_cast<DecisionPrefixError>(n); }
  if (j.contains("prefix_event")) { v.prefix_event = ParseTreeLocation(j["prefix_event"]); if (!v.prefix_event) return {}; }
  return v;
}
inline nlohmann::json DeferredCellJson(const DeferredCellEvidence& v) {
  nlohmann::json j{{"reason", static_cast<unsigned>(v.reason)}, {"creation", TreeLocationJson(v.creation)},
      {"allocation", TreeLocationJson(v.allocation)}, {"store", TreeLocationJson(v.store)}};
  if (v.creator) j["creator"] = v.creator->value;
  if (v.child) j["child"] = v.child->value;
  if (v.cell) j["cell"] = v.cell->value;
  if (v.requested) j["requested"] = static_cast<unsigned>(*v.requested);
  if (v.occurrence) {
    const auto& o = *v.occurrence;
    j["occurrence"] = {{"ordinal", o.ordinal}, {"depth", o.cell.depth.value}, {"slot", o.cell.slot.value}, {"requested", static_cast<unsigned>(o.requested)}};
    if (o.source) {
      TreeLocation source; source.method = o.source->method; source.pc = o.source->pc; source.working_pc = o.source->working_pc;
      j["occurrence"]["source"] = TreeLocationJson(source);
    }
  }
  return j;
}
inline std::optional<DeferredCellEvidence> ParseDeferredCell(const nlohmann::json& j) {
  std::uint32_t kind;
  if (!j.is_object() || !FailureNumber(j,"reason",kind) || kind > static_cast<unsigned>(DeferredCellError::kBinding)) return {};
  DeferredCellEvidence v{static_cast<DeferredCellError>(kind)};
  if (!FailureOptionalId(j,"creator",v.creator) || !FailureOptionalId(j,"child",v.child) || !FailureOptionalId(j,"cell",v.cell)) return {};
  if (j.contains("requested")) {
    std::uint32_t domain; if (!FailureNumber(j,"requested",domain) || domain > static_cast<unsigned>(analysis::CaptureDomain::kSourceAccess)) return {};
    v.requested = static_cast<analysis::CaptureDomain>(domain);
  }
  for (const auto [name, target] : {std::pair{"creation", &v.creation}, {"allocation", &v.allocation}, {"store", &v.store}}) {
    if (!j.contains(name)) return {}; auto parsed = ParseTreeLocation(j[name]); if (!parsed) return {}; *target = *parsed;
  }
  if (j.contains("occurrence")) {
    const auto& o = j["occurrence"]; std::uint32_t ordinal,depth,slot,domain;
    if (!FailureNumber(o,"ordinal",ordinal) || !FailureNumber(o,"depth",depth) || !FailureNumber(o,"slot",slot) ||
        !FailureNumber(o,"requested",domain) || domain > static_cast<unsigned>(analysis::CaptureDomain::kSourceAccess)) return {};
    if (slot > UINT16_MAX) return {};
    CaptureOccurrence use{ordinal, {{depth},{static_cast<std::uint16_t>(slot)}}, static_cast<analysis::CaptureDomain>(domain), {}};
    if (o.contains("source")) {
      auto source = ParseTreeLocation(o["source"]); if (!source || !source->method) return {};
      use.source = CaptureOrigin{*source->method,source->pc,source->working_pc};
    }
    v.occurrence = use;
  }
  return v;
}
}
#endif
