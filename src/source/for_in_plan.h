#ifndef ABCD_SOURCE_FOR_IN_PLAN_H_
#define ABCD_SOURCE_FOR_IN_PLAN_H_

#include "abcd/source/for_in_failure.h"

namespace abcd::source {
struct ForInLoop {
  analysis::OpId acquisition, advance, decision;
  analysis::ValueId enumerator, key;
  analysis::UseId object;
  analysis::WorkBlockId header, body, exit;
  analysis::EdgeId body_edge, done_edge;
  bool operator==(const ForInLoop&) const = default;
};
struct ForInKeyUse {
  std::uint32_t loop;
  analysis::UseId use;
  bool operator==(const ForInKeyUse&) const = default;
};
struct ForInForwarding {
  analysis::OpId acquisition;
  analysis::WorkBlockId handler;
  analysis::ValueId exception;
  bool operator==(const ForInForwarding&) const = default;
};
enum class ForInRole : std::uint8_t { kEnumerator, kNextResult, kDoneTest, kDecision };
struct ForInMember {
  std::uint32_t loop;
  ForInRole role;
  bool operator==(const ForInMember&) const = default;
};
// Raw protocol evidence, NOT a syntax/activation/publication certificate.
// Key uses retain their original stores and scope. A later tree projection
// must prove the chosen native binding, RHS scope and each dynamic activation.
struct ForInPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ForInLoop> loops; // acquisition order
  std::vector<ForInKeyUse> keys; // original UseId order
  std::vector<ForInForwarding> forwarding;
  // Dense occurrence indexes; absence means ordinary source planning. These
  // retain protocol identity, never a source SymbolId or expression identity.
  std::vector<std::optional<ForInMember>> operations, values, uses;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires verified availability, effects and original input correspondence.
// O((V+E+U) log V), including shared SCC identities and natural-loop discovery.
// No mutation, symbol allocation, tree admission or catch simplification.
[[nodiscard]] std::variant<ForInPlan, ForInFailure> BuildForInPlan(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t, ForInFailure> VerifyForInPlan(
    const analysis::ValueProgram&, const ForInPlan&, analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
