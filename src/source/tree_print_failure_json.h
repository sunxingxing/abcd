#ifndef ABCD_SOURCE_TREE_PRINT_FAILURE_JSON_H_
#define ABCD_SOURCE_TREE_PRINT_FAILURE_JSON_H_
#include "abcd/source/tree_print_failure.h"
#include "tree_failure_json.h"
#include "concurrent_failure_json.h"
namespace abcd::source {
inline nlohmann::json TreePrintFailureJson(const TreePrintFailure& f) {
  nlohmann::json j{{"kind",static_cast<unsigned>(f.kind)}, {"site",static_cast<unsigned>(f.site)},
    {"written_bytes",f.written_bytes},{"location",TreeLocationJson(f.location)}};
  if(const auto* cause=std::get_if<TreeStructureError>(&f.cause))j["structure_error"]=static_cast<unsigned>(*cause);
  if(f.concurrent)j["concurrent"]=ConcurrentJson(*f.concurrent);
  if(f.owner)j["owner"]=f.owner->value;
  if(f.anchor)j["anchor"]=f.anchor->value;
  if(f.budget) {
    const auto& b=*f.budget;
    j["budget"]={{"kind",static_cast<unsigned>(b.budget_kind)}, {"remaining",b.remaining},
      {"limit",b.limit},{"used",b.used},{"peak",b.peak},{"retained",b.retained}};
    if((b.budget_kind==TreePrintBudgetKind::kHostAllocation || b.budget_kind==TreePrintBudgetKind::kCapacityOverflow) &&
        !b.requested && b.limit==0 && b.used==0 && b.peak==0 && b.retained==0) {
      // No byte quota belongs to an unobserved host request. Zero-initialized
      // fields in the fixed witness are absence, not a fabricated zero limit.
      for(const auto* key:{"remaining","limit","used","peak","retained"})j["budget"][key]=nullptr;
    }
    if(b.requested)j["budget"]["requested"]=*b.requested;
  }
  return j;
}
inline std::optional<TreePrintFailure> ParseTreePrintFailure(const nlohmann::json& j) {
  std::uint32_t kind,site;
  if(!j.is_object()||!FailureNumber(j,"kind",kind)||kind>static_cast<unsigned>(TreePrintError::kInvalidLiteral)||
      !FailureNumber(j,"site",site)||site>static_cast<unsigned>(TreePrintSite::kPublication)||!j.contains("location"))return {};
  TreePrintFailure f{static_cast<TreePrintError>(kind)};f.site=static_cast<TreePrintSite>(site);
  if(j.contains("structure_error")){std::uint32_t n;if(!FailureNumber(j,"structure_error",n)||n>static_cast<unsigned>(TreeStructureError::kGlobalShadowed))return {};f.cause=static_cast<TreeStructureError>(n);}
  auto location=ParseTreeLocation(j["location"]);if(!location)return {};f.location=*location;
  if(!FailureOptionalId(j,"owner",f.owner)||!FailureOptionalId(j,"anchor",f.anchor))return {};
  const auto number=[](const nlohmann::json& value,const char* key,std::uint64_t& out){
    if(!value.contains(key)||!value[key].is_number_unsigned())return false;out=value[key].get<std::uint64_t>();return true;
  };
  std::uint64_t written;
  if(!number(j,"written_bytes",written)||written>SIZE_MAX)return {};f.written_bytes=written;
  if(j.contains("concurrent")){f.concurrent=ParseConcurrent(j["concurrent"]);if(!f.concurrent)return {};}
  if(j.contains("budget")) {
    const auto& b=j["budget"];TreeBudgetEvidence e;
    if(!FailureNumber(b,"kind",kind)||kind>static_cast<unsigned>(TreePrintBudgetKind::kHostAllocation))return {};
    e.budget_kind=static_cast<TreePrintBudgetKind>(kind);
    const bool unknown=b.contains("limit")&&b["limit"].is_null();
    if(unknown) {
      if(e.budget_kind!=TreePrintBudgetKind::kHostAllocation&&e.budget_kind!=TreePrintBudgetKind::kCapacityOverflow)return {};
      for(const auto* key:{"remaining","limit","used","peak","retained"})if(!b.contains(key)||!b[key].is_null())return {};
      if(b.contains("requested"))return {};
    } else if(!number(b,"remaining",e.remaining)||!number(b,"limit",e.limit)||!number(b,"used",e.used)||
        !number(b,"peak",e.peak)||!number(b,"retained",e.retained))return {};
    if(e.remaining>e.limit||e.used!=e.limit-e.remaining)return {};
    if(b.contains("requested")){std::uint64_t n;if(!number(b,"requested",n))return {};e.requested=n;}
    f.budget=e;
  }
  return f;
}
}
#endif
