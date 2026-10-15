#ifndef ABCD_SOURCE_CELL_PARAMETER_DEFAULT_JSON_H_
#define ABCD_SOURCE_CELL_PARAMETER_DEFAULT_JSON_H_
#include "abcd/source/cell_parameter_default_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json CellParameterDefaultJson(const CellParameterDefaultDiagnostic& d) {
  nlohmann::json j={{"verified",d.verified},{"count",d.count},{"location",TreeLocationJson(d.evidence.location)}};
  if(!d.verified)j["reason"]=static_cast<unsigned>(d.evidence.kind);
  if(d.evidence.parameter)j["parameter"]=*d.evidence.parameter;
  if(d.evidence.local)j["local"]={{"allocation",d.evidence.local->allocation.value},{"slot",d.evidence.local->slot.value}};
  if(d.evidence.outer)j["outer"]={{"depth",d.evidence.outer->depth.value},{"slot",d.evidence.outer->slot.value}};
  if(d.evidence.use)j["use"]=d.evidence.use->value;
  if(d.verified){j["initial_store"]=TreeLocationJson(d.initial_store);j["test"]=TreeLocationJson(d.test);
    j["read"]=TreeLocationJson(d.read);j["guard"]=TreeLocationJson(d.guard);j["fallback_store"]=TreeLocationJson(d.fallback_store);
    if(d.body)j["body"]=d.body->value;}
  return j;
}
inline std::optional<CellParameterDefaultDiagnostic> ParseCellParameterDefault(const nlohmann::json& j) {
  if(!j.is_object()||!j.contains("verified")||!j["verified"].is_boolean()||!j.contains("location"))return {};
  CellParameterDefaultDiagnostic d;d.verified=j["verified"].get<bool>();
  if(!FailureNumber(j,"count",d.count))return {};
  const auto location=ParseTreeLocation(j["location"]);if(!location)return {};d.evidence.location=*location;
  if(!d.verified){std::uint32_t reason;if(!FailureNumber(j,"reason",reason)||reason>static_cast<unsigned>(CellParameterDefaultError::kWorkLimit))return {};d.evidence.kind=static_cast<CellParameterDefaultError>(reason);}
  if(j.contains("parameter")){std::uint32_t p;if(!FailureNumber(j,"parameter",p))return {};d.evidence.parameter=p;}
  if(j.contains("local")){std::uint32_t a,s;if(!FailureNumber(j["local"],"allocation",a)||!FailureNumber(j["local"],"slot",s)||s>UINT16_MAX)return {};d.evidence.local=ParameterCellIdentity{{a},{static_cast<std::uint16_t>(s)}};}
  if(j.contains("outer")){std::uint32_t a,s;if(!FailureNumber(j["outer"],"depth",a)||!FailureNumber(j["outer"],"slot",s)||s>UINT16_MAX)return {};d.evidence.outer=DefaultCaptureIdentity{{a},{static_cast<std::uint16_t>(s)}};}
  if(!FailureOptionalId(j,"use",d.evidence.use)||!FailureOptionalId(j,"body",d.body))return {};
  if(d.verified){
    if(!d.count||!d.evidence.parameter||!d.evidence.local||!d.evidence.outer||!d.body)return {};
    for(auto [name,out]:{std::pair{"initial_store",&d.initial_store},{"test",&d.test},{"read",&d.read},{"guard",&d.guard},{"fallback_store",&d.fallback_store}}) {
      if(!j.contains(name))return {};auto v=ParseTreeLocation(j[name]);if(!v)return {};*out=*v;
    }
  }
  return d;
}

}
#endif
