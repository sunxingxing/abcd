#ifndef ABCD_SOURCE_CONCURRENT_FAILURE_JSON_H_
#define ABCD_SOURCE_CONCURRENT_FAILURE_JSON_H_
#include "abcd/source/concurrent_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json ConcurrentJson(const ConcurrentFailure& v) {
  nlohmann::json j{{"kind", static_cast<unsigned>(v.kind)},
                   {"method", v.method.value},
                   {"record", v.record.value}};
  if(v.creator.value) {j["creator"]=v.creator.value;j["occurrence"]=v.occurrence.value;j["creation_pc"]=v.creation_pc.value;}
  if (v.annotation) j["annotation"] = v.annotation->value;
  if (v.annotation_present) j["annotation_present"] = *v.annotation_present;
  if (v.request_count) j["request_count"] = *v.request_count;
  if (v.element) j["element"] = *v.element;
  if (v.request) j["request"] = *v.request;
  if(v.operation)j["operation"]=v.operation->value;
  if(v.returned)j["returned"]=v.returned->value;
  if(v.use)j["use"]=v.use->value;
  if(v.pc)j["pc"]=v.pc->value;
  if(v.working_pc)j["working_pc"]=v.working_pc->value;
  if (v.metadata) j["metadata_error"] = static_cast<unsigned>(*v.metadata);
  std::visit(
      [&](auto cause) {
        if constexpr (std::is_enum_v<decltype(cause)>)
          j["cause"] = {{"type", v.cause.index()},
                        {"value", static_cast<unsigned>(cause)}};
      },
      v.cause);
  return j;
}
inline std::optional<ConcurrentFailure> ParseConcurrent(
    const nlohmann::json& j) {
  ConcurrentFailure v;
  std::uint32_t kind;
  if (!j.is_object() || !FailureNumber(j, "kind", kind) ||
      kind > static_cast<unsigned>(ConcurrentError::kModuleSyntax) ||
      !FailureNumber(j, "method", v.method.value) ||
      !FailureNumber(j, "record", v.record.value))
    return {};
  v.kind = static_cast<ConcurrentError>(kind);
  if(j.contains("creator")||j.contains("occurrence")||j.contains("creation_pc")) {
    if(!FailureNumber(j,"creator",v.creator.value)||!v.creator.value||
        !FailureNumber(j,"occurrence",v.occurrence.value)||!FailureNumber(j,"creation_pc",v.creation_pc.value))return {};
  }
  if (!FailureOptionalId(j, "annotation", v.annotation)) return {};
  if(!FailureOptionalId(j,"operation",v.operation)||!FailureOptionalId(j,"returned",v.returned)||
      !FailureOptionalId(j,"use",v.use)||!FailureOptionalId(j,"pc",v.pc)||
      !FailureOptionalId(j,"working_pc",v.working_pc))return {};

  const auto number = [&](const char* name,
                          std::optional<std::uint32_t>& field) {
    if (!j.contains(name)) return true;
    std::uint32_t n;
    if (!FailureNumber(j, name, n)) return false;
    field = n;
    return true;
  };
  if (!number("element", v.element) || !number("request", v.request) ||
      !number("request_count", v.request_count))
    return {};
  if (j.contains("annotation_present")) {
    if (!j["annotation_present"].is_boolean()) return {};
    v.annotation_present = j["annotation_present"].get<bool>();
  }
  if (v.annotation_present.has_value() != v.request_count.has_value() ||
      (v.annotation_present &&
       (!*v.annotation_present && (v.annotation || *v.request_count))))
    return {};
  if (v.annotation_present && *v.annotation_present && !v.annotation) return {};
  if (j.contains("metadata_error")) {
    std::uint32_t n;
    if (!FailureNumber(j, "metadata_error", n) ||
        n > static_cast<unsigned>(abc::MetadataError::kDuplicateTag))
      return {};
    v.metadata = static_cast<abc::MetadataError>(n);
  }
  if (j.contains("cause")) {
    std::uint32_t type, n;
    const auto& c = j["cause"];
    if (!c.is_object() || !FailureNumber(c, "type", type) ||
        !FailureNumber(c, "value", n))
      return {};
    switch (type) {
      case 1:
        if (n > static_cast<unsigned>(abc::ConcurrentRequestError::kWorkLimit))
          return {};
        v.cause = static_cast<abc::ConcurrentRequestError>(n);
        break;
      case 2:
        if (n > static_cast<unsigned>(abc::MetadataError::kDuplicateTag))
          return {};
        v.cause = static_cast<abc::MetadataError>(n);
        break;
      case 3:
        if (n > static_cast<unsigned>(abc::AttributeError::kReference))
          return {};
        v.cause = static_cast<abc::AttributeError>(n);
        break;
      case 4:
        if (n > static_cast<unsigned>(abc::ModuleError::kRequestPhases))
          return {};
        v.cause = static_cast<abc::ModuleError>(n);
        break;
      default:
        return {};
    }
  }
  return v;
}
}  // namespace abcd::source
#endif
