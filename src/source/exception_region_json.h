#ifndef ABCD_SOURCE_EXCEPTION_REGION_JSON_H_
#define ABCD_SOURCE_EXCEPTION_REGION_JSON_H_
#include "abcd/source/exception_region_failure.h"
#include <nlohmann/json.hpp>
namespace abcd::source {
inline nlohmann::json ExceptionRegionJson(const ExceptionRegionEvidence& e) {
  nlohmann::json j{{"region",e.region.value},{"working_body_pc",e.working_body_pc.value},{"working_handler_pc",e.working_handler_pc.value}};
  if(e.body_pc)j["body_pc"]=e.body_pc->value;
  if(e.handler_pc)j["handler_pc"]=e.handler_pc->value;
  if(e.exits) {
    j["exits"]=nlohmann::json::array();
    for(const auto& exit:*e.exits) {
      nlohmann::json x{{"origin",exit.source.index()},{"source",std::visit([](auto id){return id.value;},exit.source)},
          {"target",exit.target.value},{"continuation",exit.continuation.value}};
      if(exit.from)x["from"]=exit.from->value;
      if(exit.pc)x["pc"]=exit.pc->value;
      if(exit.working_pc)x["working_pc"]=exit.working_pc->value;
      if(exit.target_pc)x["target_pc"]=exit.target_pc->value;
      if(exit.target_working_pc)x["target_working_pc"]=exit.target_working_pc->value;
      if(exit.continuation_pc)x["continuation_pc"]=exit.continuation_pc->value;
      if(exit.continuation_working_pc)x["continuation_working_pc"]=exit.continuation_working_pc->value;
      j["exits"].push_back(std::move(x));
    }
  }
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
  if(j.contains("exits")) {
    const auto& exits=j["exits"];if(!exits.is_array()||exits.size()!=2)return {};
    e.exits.emplace();
    for(std::size_t i=0;i<2;++i) {
      const auto& x=exits[i];if(!x.is_object())return {};
      const auto n=[&](const char* key)->std::optional<std::uint32_t>{
        const auto it=x.find(key);if(it==x.end()||!it->is_number_unsigned()||it->get<std::uint64_t>()>UINT32_MAX)return {};
        return it->get<std::uint32_t>();
      };
      const auto kind=n("origin"),id=n("source"),target=n("target"),continuation=n("continuation");
      if(!kind||*kind>1||!id||!target||!continuation)return {};
      auto& v=(*e.exits)[i];
      if(*kind==0)v.source=analysis::EdgeId{*id};else v.source=analysis::TryId{*id};
      v.target={*target};v.continuation={*continuation};
      if(x.contains("from")){const auto from=n("from");if(!from)return {};v.from=analysis::WorkBlockId{*from};}
      if((*kind==0)!=v.from.has_value())return {};
      const auto pc=[&](const char* key,auto& dest){if(!x.contains(key))return true;const auto value=n(key);if(!value)return false;dest=abc::CodeOffset{*value};return true;};
      if(!pc("pc",v.pc)||!pc("working_pc",v.working_pc)||!pc("target_pc",v.target_pc)||
          !pc("target_working_pc",v.target_working_pc)||!pc("continuation_pc",v.continuation_pc)||
          !pc("continuation_working_pc",v.continuation_working_pc))return {};
      if(*kind==1&&(v.pc||v.working_pc))return {};
    }
    if((*e.exits)[0].continuation==(*e.exits)[1].continuation)return {};
  }
  return e;
}
}
#endif
