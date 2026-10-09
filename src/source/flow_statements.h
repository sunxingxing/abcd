#ifndef ABCD_SOURCE_FLOW_STATEMENTS_H_
#define ABCD_SOURCE_FLOW_STATEMENTS_H_
#include "flow_cycles.h"
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
  // Full descendant observations, excluding only this statement's adapter-owned
  // entry evaluation. Child evaluations are never excluded from their parent.
  std::vector<StatementObservations> residuals;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Consume retained flow events, definitions and language ownership once. All
// syntax, including unreachable declarations, retains its binding obligation.
// This analysis proves finite normal effect-free completion only; independent
// demand analysis owns deleting writes/initializations. Cleanup/repetition
// precision without cycle/boundary evidence is conservative. Local jump targets
// discharge only their owning normal-completion region, independently of shape.
[[nodiscard]] std::variant<FlowStatements,TreeStructureError> AnalyzeFlowStatements(
    const RecoveryFlow&,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<FlowStatements,TreeStructureError> AnalyzeFlowStatements(
    const RecoveryFlow&,const FlowCycles&,analysis::SemanticBudget = {});
}
#endif
