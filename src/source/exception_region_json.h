#ifndef ABCD_SOURCE_EXCEPTION_REGION_JSON_H_
#define ABCD_SOURCE_EXCEPTION_REGION_JSON_H_
#include "abcd/source/exception_region_failure.h"
#include <nlohmann/json.hpp>
namespace abcd::source {
inline nlohmann::json ExceptionRegionJson(const ExceptionRegionEvidence& e) {
  nlohmann::json j{{"region",e.region.value},{"working_body_pc",e.working_body_pc.value},{"working_handler_pc",e.working_handler_pc.value}};
  if(e.body_pc)j["body_pc"]=e.body_pc->value;
  if(e.handler_pc)j["handler_pc"]=e.handler_pc->value;
  return j;
}
inline std::optional<ExceptionRegionEvidence> ParseExceptionRegionJson(const nlohmann::json& j) {
  if(!j.is_object())return {};
  const auto number=[&](const char* key)->std::optional<std::uint32_t>{
    const auto it=j.find(key);if(it==j.end()||!it->is_number_unsigned()||it->get<std::uint64_t>()>UINT32_MAX)return {};
    return it->get<std::uint32_t>();
  };
  const auto r=number("region"),b=number("working_body_pc"),h=number("working_handler_pc");
  if(!r||!b||!h)return {};
  ExceptionRegionEvidence e{{*r},{},{},{*b},{*h}};
  if(j.contains("body_pc")){const auto v=number("body_pc");if(!v)return {};e.body_pc=abc::CodeOffset{*v};}
  if(j.contains("handler_pc")){const auto v=number("handler_pc");if(!v)return {};e.handler_pc=abc::CodeOffset{*v};}
  return e;
}
}
#endif
