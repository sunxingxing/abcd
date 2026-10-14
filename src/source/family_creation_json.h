#ifndef ABCD_SOURCE_FAMILY_CREATION_JSON_H_
#define ABCD_SOURCE_FAMILY_CREATION_JSON_H_
#include "abcd/source/family_creation_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json FamilyCreationJson(const FamilyCreationEvidence& e) {
  nlohmann::json j{{"reason", static_cast<unsigned>(e.reason)}, {"location", TreeLocationJson(e.location)}};
  if (e.instruction) j["instruction"] = e.instruction->value;
  if (e.root) j["root"] = e.root->value;
  if (e.occurrence) j["occurrence"] = e.occurrence->value;
  if (e.kind) j["kind"] = static_cast<unsigned>(*e.kind);
  if (e.constructor) j["constructor_pool"] = e.constructor->value;
  if (e.literal) j["literal_pool"] = e.literal->value;
  if (e.projected_class) j["projected_class"] = e.projected_class->value;
  if (e.descendant_source) j["descendant_source"] = TreeLocationJson(*e.descendant_source);
  return j;
}
inline std::optional<FamilyCreationEvidence> ParseFamilyCreation(const nlohmann::json& j) {
  if (!j.is_object()) return {};
  std::uint32_t reason;
  if (!FailureNumber(j, "reason", reason) || reason > static_cast<unsigned>(FamilyCreationCheck::kClassCompletion)) return {};
  const auto at = j.find("location"); if (at == j.end()) return {};
  const auto location = ParseTreeLocation(*at); if (!location) return {};
  FamilyCreationEvidence e{static_cast<FamilyCreationCheck>(reason), *location};
  if (!FailureOptionalId(j, "instruction", e.instruction) || !FailureOptionalId(j, "root", e.root) ||
      !FailureOptionalId(j, "occurrence", e.occurrence) || !FailureOptionalId(j, "constructor_pool", e.constructor) ||
      !FailureOptionalId(j, "literal_pool", e.literal) || !FailureOptionalId(j, "projected_class", e.projected_class)) return {};
  if (j.contains("kind")) {
    std::uint32_t kind;
    if (!FailureNumber(j, "kind", kind) || kind > static_cast<unsigned>(FamilyCreationKind::kOther)) return {};
    e.kind = static_cast<FamilyCreationKind>(kind);
  }
  if (e.constructor.has_value() != e.literal.has_value() ||
      (e.constructor && e.kind != FamilyCreationKind::kClass)) return {};
  if (const auto at = j.find("descendant_source"); at != j.end()) {
    e.descendant_source = ParseTreeLocation(*at); if (!e.descendant_source) return {};
  }
  return e;
}
} // namespace abcd::source
#endif
