#ifndef ABCD_SOURCE_FLOW_STATEMENTS_H_
#define ABCD_SOURCE_FLOW_STATEMENTS_H_
#include "recovery_flow.h"
namespace abcd::source {
struct StatementObservations {
  ExpressionEvaluation evaluation=ExpressionEvaluation::kInert;
  bool definitions=false,abrupt=false,cleanup=false,repetition=false,suspension=false;
  [[nodiscard]] bool Inert() const {
    return evaluation==ExpressionEvaluation::kInert&&!definitions&&!abrupt&&!cleanup&&!repetition&&!suspension;
  }
};
struct FlowStatements {
  std::vector<StatementObservations> observations;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Consume retained flow events, definitions and language ownership once. All
// syntax, including unreachable declarations, retains its binding obligation.
// This analysis proves finite normal effect-free completion only; independent
// demand analysis owns deleting writes/initializations. Cleanup/repetition
// precision is deliberately conservative, not a control-shape admission rule.
[[nodiscard]] std::variant<FlowStatements,TreeStructureError> AnalyzeFlowStatements(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
