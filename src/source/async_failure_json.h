#ifndef ABCD_SOURCE_ASYNC_FAILURE_JSON_H_
#define ABCD_SOURCE_ASYNC_FAILURE_JSON_H_
#include "abcd/source/async_failure.h"
#include "tree_failure_json.h"
namespace abcd::source {
inline nlohmann::json AsyncCompletionJson(const AsyncCompletionEvidence& e) {
  nlohmann::json j{{"check", static_cast<unsigned>(e.check)}, {"location", TreeLocationJson(e.location)}};
  if(e.related_pc) j["related_pc"] = e.related_pc->value;
  if(e.catch_input) j["catch_input"] = {{"handler_pc",e.catch_input->handler.value},
      {"register",e.catch_input->slot.value},{"prefix",static_cast<unsigned>(e.catch_input->prefix)},
      {"suffix_known",e.catch_input->suffix_known},{"suffix_confined",e.catch_input->suffix_confined}};
  return j;
}
inline std::optional<AsyncCompletionEvidence> ParseAsyncCompletion(const nlohmann::json& j) {
  if(!j.is_object())return {};
  std::uint32_t check;
  if(!FailureNumber(j,"check",check) || check > static_cast<unsigned>(AsyncCompletionCheck::kBudget))return {};
  const auto at=j.find("location");if(at==j.end())return {};
  const auto location=ParseTreeLocation(*at);if(!location)return {};
  AsyncCompletionEvidence e{static_cast<AsyncCompletionCheck>(check),*location,{}};
  if(!FailureOptionalId(j,"related_pc",e.related_pc))return {};
  if(const auto at=j.find("catch_input");at!=j.end()){
    if(!at->is_object())return {};
    std::uint32_t handler,slot,prefix;
    if(!FailureNumber(*at,"handler_pc",handler)||!FailureNumber(*at,"register",slot)||
        !FailureNumber(*at,"prefix",prefix)||prefix>static_cast<unsigned>(analysis::CatchInputPrefix::kRead)||
        !at->contains("suffix_known")||!at->at("suffix_known").is_boolean()||
        !at->contains("suffix_confined")||!at->at("suffix_confined").is_boolean())return {};
    e.catch_input=analysis::AwaitCatchObservation{{handler},{slot},static_cast<analysis::CatchInputPrefix>(prefix),
        at->at("suffix_known").get<bool>(),at->at("suffix_confined").get<bool>()};
  }
  return e;
}
} // namespace abcd::source
#endif
