#ifndef ABCD_SOURCE_SENDABLE_FAILURE_JSON_H_
#define ABCD_SOURCE_SENDABLE_FAILURE_JSON_H_
#include "abcd/source/sendable_failure.h"
#include "abcd/abc/sendable_fields.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json SendableJson(const SendableFailure& v) {
  nlohmann::json j{{"kind", static_cast<unsigned>(v.kind)}, {"record", v.record.value}, {"method", v.method.value}};
  const auto id = [&](const char* key, auto value) { if (value) j[key] = value->value; };
  id("pc", v.pc); id("working_pc", v.working_pc); id("constructor", v.constructor);
  id("literal", v.literal); id("field_types", v.field_types); id("pool", v.pool);
  const auto number = [&](const char* key, auto value) { if (value) j[key] = *value; };
  number("field", v.field); number("mask", v.mask); number("field_count", v.field_count);
  number("instance_count", v.instance_count); number("member_count", v.member_count);
  std::visit([&](auto e) {
    if constexpr (std::is_enum_v<decltype(e)>) j["cause"] = {{"type", v.cause.index()}, {"value", static_cast<unsigned>(e)}};
  }, v.cause);
  return j;
}
inline std::optional<SendableFailure> ParseSendable(const nlohmann::json& j) {
  SendableFailure v; std::uint32_t kind;
  if (!j.is_object() || !FailureNumber(j, "kind", kind) || kind > static_cast<unsigned>(SendableError::kWorkLimit) ||
      !FailureNumber(j, "record", v.record.value) || !FailureNumber(j, "method", v.method.value)) return {};
  v.kind = static_cast<SendableError>(kind);
  if (!FailureOptionalId(j, "pc", v.pc) || !FailureOptionalId(j, "working_pc", v.working_pc) ||
      !FailureOptionalId(j, "constructor", v.constructor) || !FailureOptionalId(j, "literal", v.literal) ||
      !FailureOptionalId(j, "field_types", v.field_types) || !FailureOptionalId(j, "pool", v.pool)) return {};
  const auto number = [&](const char* key, std::optional<std::uint32_t>& value) {
    if (!j.contains(key)) return true;
    std::uint32_t n; if (!FailureNumber(j, key, n)) return false; value = n; return true;
  };
  if (!number("field", v.field) || !number("mask", v.mask) || !number("field_count", v.field_count) ||
      !number("instance_count", v.instance_count) || !number("member_count", v.member_count) ||
      (v.instance_count && v.field_count && *v.instance_count > *v.field_count)) return {};
  if (j.contains("cause")) {
    std::uint32_t type, n; const auto& c = j["cause"];
    if (!c.is_object() || !FailureNumber(c, "type", type) || !FailureNumber(c, "value", n)) return {};
    switch (type) {
      case 1: if (n > static_cast<unsigned>(abc::MetadataError::kDuplicateTag)) return {}; v.cause = static_cast<abc::MetadataError>(n); break;
      case 2: if (n > static_cast<unsigned>(abc::LiteralError::kResourceLimit)) return {}; v.cause = static_cast<abc::LiteralError>(n); break;
      case 3: if (n > static_cast<unsigned>(abc::ClassBufferError::kResourceLimit)) return {}; v.cause = static_cast<abc::ClassBufferError>(n); break;
      case 4: if (n > static_cast<unsigned>(abc::SendableFieldError::kWorkLimit)) return {}; v.cause = static_cast<abc::SendableFieldError>(n); break;
      case 5: if (n > static_cast<unsigned>(abc::BodyError::kResourceLimit)) return {}; v.cause = static_cast<abc::BodyError>(n); break;
      case 6: if (n > static_cast<unsigned>(analysis::CreationError::kMalformedInstruction)) return {}; v.cause = static_cast<analysis::CreationError>(n); break;
      default: return {};
    }
  }
  return v;
}
inline nlohmann::json SharedModuleJson(const SharedModuleFailure& v) {
  nlohmann::json j{{"kind", static_cast<unsigned>(v.kind)}, {"record", v.record.value},
      {"requests", v.requests}, {"imports", v.imports}, {"namespace_imports", v.namespace_imports},
      {"local_exports", v.local_exports}, {"indirect_exports", v.indirect_exports}, {"star_exports", v.star_exports}};
  if (v.shared) j["shared"] = *v.shared;
  if (v.module) j["module"] = v.module->value;
  if (v.request_phases) j["request_phases"] = v.request_phases->value;
  std::visit([&](auto e) {
    if constexpr (std::is_enum_v<decltype(e)>) j["cause"] = {{"type", v.cause.index()}, {"value", static_cast<unsigned>(e)}};
  }, v.cause);
  return j;
}
inline std::optional<SharedModuleFailure> ParseSharedModule(const nlohmann::json& j) {
  SharedModuleFailure v; std::uint32_t kind;
  if (!j.is_object() || !FailureNumber(j, "kind", kind) || kind > static_cast<unsigned>(SharedModuleError::kWorkLimit) ||
      !FailureNumber(j, "record", v.record.value) || !FailureNumber(j, "requests", v.requests) ||
      !FailureNumber(j, "imports", v.imports) || !FailureNumber(j, "namespace_imports", v.namespace_imports) ||
      !FailureNumber(j, "local_exports", v.local_exports) || !FailureNumber(j, "indirect_exports", v.indirect_exports) ||
      !FailureNumber(j, "star_exports", v.star_exports) || !FailureOptionalId(j, "module", v.module) ||
      !FailureOptionalId(j, "request_phases", v.request_phases)) return {};
  v.kind = static_cast<SharedModuleError>(kind);
  if (j.contains("shared")) { if (!j["shared"].is_boolean()) return {}; v.shared = j["shared"].get<bool>(); }
  if (j.contains("cause")) {
    std::uint32_t type, n; const auto& c = j["cause"];
    if (!c.is_object() || !FailureNumber(c, "type", type) || !FailureNumber(c, "value", n)) return {};
    switch (type) {
      case 1: if (n > static_cast<unsigned>(abc::MetadataError::kDuplicateTag)) return {}; v.cause = static_cast<abc::MetadataError>(n); break;
      case 2: if (n > static_cast<unsigned>(abc::ModuleError::kRequestPhases)) return {}; v.cause = static_cast<abc::ModuleError>(n); break;
      default: return {};
    }
  }
  return v;
}
}
#endif
