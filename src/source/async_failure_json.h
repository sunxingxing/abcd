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
  if (e.catch_flow) {
    nlohmann::json flow{{"verified", e.catch_flow->verified}, {"close_projection_authorized", false},
        {"queries", nlohmann::json::array()}};
    if (e.catch_flow->failure) {
      flow["failure"] = {{"reason", static_cast<unsigned>(e.catch_flow->failure->kind)}};
      if (e.catch_flow->failure->pc) flow["failure"]["original_pc"] = e.catch_flow->failure->pc->value;
    }
    for (const auto& q : e.catch_flow->queries) {
      nlohmann::json row{{"await_pc", q.query.await.value}, {"definition_pc", q.query.definition.value},
          {"handler_pc", q.query.handler.value}, {"register", q.query.slot.value},
          {"role", static_cast<unsigned>(q.query.role)}, {"outcome", static_cast<unsigned>(q.outcome)}};
      if (q.read) row["read_pc"] = q.read->value;
      if (q.unknown) row["unknown_pc"] = q.unknown->value;
      if (q.cover) row["cover_pc"] = q.cover->value;
      if (q.terminal) row["terminal_pc"] = q.terminal->value;
      flow["queries"].push_back(std::move(row));
    }
    j["catch_flow"] = std::move(flow);
  }
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
  if (const auto at = j.find("catch_flow"); at != j.end()) {
    if (!at->is_object() || !at->contains("verified") || !at->at("verified").is_boolean() ||
        !at->contains("close_projection_authorized") || at->at("close_projection_authorized") != false ||
        !at->contains("queries") || !at->at("queries").is_array()) return {};
    auto flow = std::make_shared<analysis::AwaitCatchFlowDiagnostic>();
    flow->verified = at->at("verified").get<bool>();
    if (const auto error = at->find("failure"); error != at->end()) {
      std::uint32_t reason;
      if (!FailureNumber(*error, "reason", reason) || reason > static_cast<unsigned>(analysis::CatchFlowError::kWorkLimit)) return {};
      analysis::CatchFlowFailure failure{static_cast<analysis::CatchFlowError>(reason), {}};
      if (!FailureOptionalId(*error, "original_pc", failure.pc)) return {};
      flow->failure = failure;
    }
    for (const auto& row : at->at("queries")) {
      std::uint32_t await, definition, handler, slot, role, outcome;
      if (!FailureNumber(row, "await_pc", await) || !FailureNumber(row, "definition_pc", definition) ||
          !FailureNumber(row, "handler_pc", handler) || !FailureNumber(row, "register", slot) ||
          !FailureNumber(row, "role", role) || role > static_cast<unsigned>(analysis::CatchFlowRole::kResumeMode) ||
          !FailureNumber(row, "outcome", outcome) || outcome > static_cast<unsigned>(analysis::CatchFlowOutcome::kUnknownEffect)) return {};
      analysis::CatchFlowEvidence q{{{await}, {definition}, {handler}, {slot}, static_cast<analysis::CatchFlowRole>(role)},
          static_cast<analysis::CatchFlowOutcome>(outcome)};
      if (!FailureOptionalId(row,"read_pc",q.read) || !FailureOptionalId(row,"unknown_pc",q.unknown) ||
          !FailureOptionalId(row,"cover_pc",q.cover) || !FailureOptionalId(row,"terminal_pc",q.terminal)) return {};
      flow->queries.push_back(q);
    }
    if (flow->verified == flow->failure.has_value()) return {};
    e.catch_flow = std::move(flow);
  }
  return e;
}
} // namespace abcd::source
#endif
