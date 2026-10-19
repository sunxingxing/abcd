#ifndef ABCD_SOURCE_CLASS_SELF_INITIALIZATION_JSON_H_
#define ABCD_SOURCE_CLASS_SELF_INITIALIZATION_JSON_H_
#include "abcd/source/class_self_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json ClassSelfInitializationJson(
    const ClassSelfInitializationDiagnostic& v) {
  nlohmann::json j{{"status", static_cast<unsigned>(v.status)},
                   {"creator", v.creator.value},
                   {"initializer", v.initializer.value},
                   {"creation", v.creation.value},
                   {"allocation", v.allocation.value},
                   {"store", v.store.value},
                   {"slot", v.slot.value},
                   {"reads", v.reads},
                   {"captures", v.captures},
                   {"cleanup", v.cleanup},
                   {"families", v.families}};
  if (v.failure) {
    const auto& f = *v.failure;
    nlohmann::json e{{"kind", static_cast<unsigned>(f.kind)},
                     {"location", TreeLocationJson(f.location)},
                     {"creator", f.creator.value},
                     {"initializer", f.initializer.value}};
    const auto id = [&](const char* key, auto value) {
      if (value) e[key] = value->value;
    };
    id("creation", f.creation);
    id("allocation", f.allocation);
    id("store", f.store);
    id("read", f.read);
    id("guard", f.guard);
    id("slot", f.slot);
    id("use", f.use);
    j["failure"] = std::move(e);
  }
  return j;
}
inline std::optional<ClassSelfInitializationDiagnostic>
ParseClassSelfInitialization(const nlohmann::json& j) {
  ClassSelfInitializationDiagnostic v;
  std::uint32_t status, slot;
  if (!j.is_object() || !FailureNumber(j, "status", status) ||
      status >
          static_cast<unsigned>(ClassSelfInitializationStatus::kVerified) ||
      !FailureNumber(j, "creator", v.creator.value) ||
      !FailureNumber(j, "initializer", v.initializer.value) ||
      !FailureNumber(j, "creation", v.creation.value) ||
      !FailureNumber(j, "allocation", v.allocation.value) ||
      !FailureNumber(j, "store", v.store.value) ||
      !FailureNumber(j, "slot", slot) || slot > UINT16_MAX ||
      !FailureNumber(j, "reads", v.reads) ||
      !FailureNumber(j, "captures", v.captures) ||
      !FailureNumber(j, "cleanup", v.cleanup) ||
      !FailureNumber(j, "families", v.families))
    return {};
  v.status = static_cast<ClassSelfInitializationStatus>(status);
  v.slot = {static_cast<std::uint16_t>(slot)};
  if (j.contains("failure")) {
    const auto& e = j["failure"];
    ClassSelfInitializationFailure f;
    std::uint32_t kind;
    if (!e.is_object() || !FailureNumber(e, "kind", kind) ||
        kind > static_cast<unsigned>(
                   ClassSelfInitializationError::kRecoveryGate) ||
        !e.contains("location") ||
        !FailureNumber(e, "creator", f.creator.value) ||
        !FailureNumber(e, "initializer", f.initializer.value))
      return {};
    f.kind = static_cast<ClassSelfInitializationError>(kind);
    const auto location = ParseTreeLocation(e["location"]);
    if (!location) return {};
    f.location = *location;
    if (!FailureOptionalId(e, "creation", f.creation) ||
        !FailureOptionalId(e, "allocation", f.allocation) ||
        !FailureOptionalId(e, "store", f.store) ||
        !FailureOptionalId(e, "read", f.read) ||
        !FailureOptionalId(e, "guard", f.guard) ||
        !FailureOptionalId(e, "use", f.use))
      return {};
    if (e.contains("slot")) {
      std::uint32_t n;
      if (!FailureNumber(e, "slot", n) || n > UINT16_MAX) return {};
      f.slot = analysis::LexicalSlot{static_cast<std::uint16_t>(n)};
    }
    v.failure = f;
  }
  if ((v.status == ClassSelfInitializationStatus::kMissing) !=
          v.failure.has_value() ||
      (v.status == ClassSelfInitializationStatus::kVerified &&
       (!v.reads || !v.families)))
    return {};
  return v;
}
}  // namespace abcd::source
#endif
