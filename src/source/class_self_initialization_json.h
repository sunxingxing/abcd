#ifndef ABCD_SOURCE_CLASS_SELF_INITIALIZATION_JSON_H_
#define ABCD_SOURCE_CLASS_SELF_INITIALIZATION_JSON_H_
#include "abcd/source/class_self_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json ClassSelfFailureJson(
    const ClassSelfInitializationFailure& f) {
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
  if (f.projection) e["projection"] = static_cast<unsigned>(*f.projection);
  if (f.abi_lengths)
    e["abi_lengths"] = {{"method", f.abi_lengths->method.value},
                        {"declared", f.abi_lengths->declared},
                        {"signature", f.abi_lengths->signature}};
  return e;
}
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
  if (v.failure) j["failure"] = ClassSelfFailureJson(*v.failure);
  if (v.scope_view) {
    const auto& scope = *v.scope_view;
    nlohmann::json e{{"status", static_cast<unsigned>(scope.status)},
                     {"owners", scope.owners},
                     {"references", scope.references},
                     {"inner", scope.inner},
                     {"outer", scope.outer},
                     {"public_commits", scope.public_commits}};
    if (scope.failure) e["failure"] = ClassSelfFailureJson(*scope.failure);
    j["scope_view"] = std::move(e);
  }
  if (v.recovery) {
    j["recovery"] = ClassSelfFailureJson(*v.recovery);
    j["tree_references"] = v.tree_references;
    j["syntax_owners"] = v.syntax_owners;
    if (v.bound_outer) j["bound_outer"] = v.bound_outer;
  }
  return j;
}
inline std::optional<ClassSelfInitializationFailure> ParseClassSelfFailure(
    const nlohmann::json& e) {
  ClassSelfInitializationFailure f;
  std::uint32_t kind;
  if (!e.is_object() || !FailureNumber(e, "kind", kind) ||
      kind >
          static_cast<unsigned>(ClassSelfInitializationError::kRecoveryGate) ||
      !e.contains("location") ||
      !FailureNumber(e, "creator", f.creator.value) ||
      !FailureNumber(e, "initializer", f.initializer.value))
    return {};
  f.kind = static_cast<ClassSelfInitializationError>(kind);
  const auto location = ParseTreeLocation(e["location"]);
  if (!location) return {};
  f.location = *location;
  if (e.contains("projection")) {
    std::uint32_t p;
    if (!FailureNumber(e, "projection", p) ||
        p > static_cast<unsigned>(
                ClassSelfInitializationFailure::Projection::kMemberAbi))
      return {};
    f.projection = static_cast<ClassSelfInitializationFailure::Projection>(p);
  }
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
  if (e.contains("abi_lengths")) {
    ClassSelfInitializationFailure::AbiLengths lengths;
    const auto& value = e["abi_lengths"];
    if (f.projection !=
            ClassSelfInitializationFailure::Projection::kMemberAbi ||
        !value.is_object() ||
        !FailureNumber(value, "method", lengths.method.value) ||
        !FailureNumber(value, "declared", lengths.declared) ||
        !FailureNumber(value, "signature", lengths.signature) ||
        lengths.declared == lengths.signature)
      return {};
    f.abi_lengths = lengths;
  }
  if (f.projection == ClassSelfInitializationFailure::Projection::kMemberAbi &&
      !f.abi_lengths)
    return {};
  return f;
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
    v.failure = ParseClassSelfFailure(j["failure"]);
    if (!v.failure) return {};
  }
  if (j.contains("scope_view")) {
    const auto& e = j["scope_view"];
    ClassSelfScopeDiagnostic scope;
    std::uint32_t state;
    if (!e.is_object() || !FailureNumber(e, "status", state) ||
        state >
            static_cast<unsigned>(ClassSelfInitializationStatus::kVerified) ||
        !FailureNumber(e, "owners", scope.owners) ||
        !FailureNumber(e, "references", scope.references) ||
        !FailureNumber(e, "inner", scope.inner) ||
        !FailureNumber(e, "outer", scope.outer) ||
        !FailureNumber(e, "public_commits", scope.public_commits))
      return {};
    scope.status = static_cast<ClassSelfInitializationStatus>(state);
    if (e.contains("failure")) {
      scope.failure = ParseClassSelfFailure(e["failure"]);
      if (!scope.failure) return {};
    }
    if ((scope.status == ClassSelfInitializationStatus::kMissing) !=
            scope.failure.has_value() ||
        (scope.status == ClassSelfInitializationStatus::kVerified &&
         (!scope.owners || !scope.references || !scope.inner)) ||
        v.status != ClassSelfInitializationStatus::kVerified)
      return {};
    v.scope_view = scope;
  }
  if ((v.status == ClassSelfInitializationStatus::kMissing) !=
          v.failure.has_value() ||
      (v.status == ClassSelfInitializationStatus::kVerified &&
       (!v.reads || !v.families)))
    return {};
  if (j.contains("recovery")) {
    v.recovery = ParseClassSelfFailure(j["recovery"]);
    if (!v.recovery || v.status != ClassSelfInitializationStatus::kVerified ||
        !FailureNumber(j, "tree_references", v.tree_references) ||
        !FailureNumber(j, "syntax_owners", v.syntax_owners))
      return {};
    if (v.recovery->kind == ClassSelfInitializationError::kRecoveryGate &&
        !v.recovery->projection)
      return {};
  }
  if (!v.recovery &&
      (j.contains("tree_references") || j.contains("syntax_owners")))
    return {};
  if (j.contains("bound_outer") &&
      (!v.recovery || !FailureNumber(j, "bound_outer", v.bound_outer) ||
       !v.scope_view ||
       v.scope_view->status != ClassSelfInitializationStatus::kVerified ||
       v.bound_outer > v.scope_view->outer))
    return {};
  return v;
}
}  // namespace abcd::source
#endif
