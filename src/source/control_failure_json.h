#ifndef ABCD_SOURCE_CONTROL_FAILURE_JSON_H_
#define ABCD_SOURCE_CONTROL_FAILURE_JSON_H_
#include "abcd/source/control_failure.h"
#include <nlohmann/json.hpp>
#include <limits>
namespace abcd::source {
inline nlohmann::json ControlJson(const ControlOwnershipEvidence& proof) {
  const auto location = [](const ControlLocation& node) {
    nlohmann::json value{{"node", node.node}, {"origin", static_cast<unsigned>(node.origin)}, {"source", node.source}};
    if (node.pc) value["pc"] = node.pc->value;
    if (node.working_pc) value["working_pc"] = node.working_pc->value;
    return value;
  };
  const auto bounds = [&](const ControlRegionBounds& region) {
    return nlohmann::json{{"first", location(region.first)}, {"stop", region.stop ? location(*region.stop) : nlohmann::json(nullptr)}};
  };
  nlohmann::json result{{"node", location(proof.node)}, {"current", bounds(proof.current)}, {"previous", bounds(proof.previous)}};
  if (proof.arc) result["arc"] = *proof.arc;
  if (proof.decision) {
    const auto& d = *proof.decision;
    result["decision"] = {{"entry", location(d.entry)}, {"predicate", location(d.predicate)},
        {"shared", location(d.shared)}, {"fallback", location(d.fallback)},
        {"old_join", location(d.old_join)}, {"enclosing_stop", location(d.enclosing_stop)},
        {"witness", location(d.witness)}};
  }
  return result;
}
inline std::optional<ControlOwnershipEvidence> ParseControlJson(const nlohmann::json& json) {
  const auto number = [](const nlohmann::json& value, const char* key, std::uint32_t& target) {
    const auto field = value.find(key);
    if (field == value.end() || !field->is_number_unsigned() ||
        field->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) return false;
    target = field->get<std::uint32_t>(); return true;
  };
  const auto location = [&](const nlohmann::json& value, ControlLocation& node) {
    std::uint32_t kind;
    if (!value.is_object() || !number(value, "node", node.node) || !number(value, "source", node.source) ||
        !number(value, "origin", kind) || kind > static_cast<unsigned>(ControlOriginKind::kProtocolClose)) return false;
    node.origin = static_cast<ControlOriginKind>(kind);
    const auto pc = [&](const char* key, std::optional<abc::CodeOffset>& target) {
      if (!value.contains(key)) return true;
      std::uint32_t offset; if (!number(value, key, offset)) return false;
      target = abc::CodeOffset{offset}; return true;
    };
    if (!pc("pc", node.pc) || !pc("working_pc", node.working_pc)) return false;
    if (node.origin != ControlOriginKind::kBlock && node.origin != ControlOriginKind::kPhase &&
        (node.pc || node.working_pc)) return false;
    return true;
  };
  const auto bounds = [&](const nlohmann::json& value, ControlRegionBounds& region) {
    if (!value.is_object() || !value.contains("first") || !value.contains("stop") || !location(value["first"], region.first)) return false;
    if (!value["stop"].is_null()) { region.stop.emplace(); if (!location(value["stop"], *region.stop)) return false; }
    return true;
  };
  ControlOwnershipEvidence result;
  if (!json.is_object() || !json.contains("node") || !json.contains("current") || !json.contains("previous") ||
      !location(json["node"], result.node) || !bounds(json["current"], result.current) || !bounds(json["previous"], result.previous)) return {};
  if (json.contains("arc")) { std::uint32_t arc; if (!number(json, "arc", arc)) return {}; result.arc = arc; }
  if (json.contains("decision")) {
    const auto& value = json["decision"];
    DecisionContinuationEvidence d;
    const auto member = [&](const char* name, ControlLocation& target) {
      return value.is_object() && value.contains(name) && location(value[name], target);
    };
    if (!member("entry", d.entry) || !member("predicate", d.predicate) ||
        !member("shared", d.shared) || !member("fallback", d.fallback) ||
        !member("old_join", d.old_join) || !member("enclosing_stop", d.enclosing_stop) ||
        !member("witness", d.witness)) return {};
    result.decision = d;
  }
  return result;
}
}
#endif
