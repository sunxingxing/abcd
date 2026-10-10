#ifndef ABCD_SOURCE_FLOW_KINDS_H_
#define ABCD_SOURCE_FLOW_KINDS_H_
#include "flow_liveness.h"
#include "flow_value_states.h"
#include "truth_domains.h"
namespace abcd::source {
struct RecoveryExpressionInput;
struct FlowInitializationProgram;
struct FlowNormalKinds {
  std::vector<analysis::ValueKinds> expressions;
  std::vector<TruthDomains> truths;
  std::vector<analysis::ValueKinds> captured_inputs;
  // Present only for the coupled completion solve; indexed by input edges.
  std::vector<std::uint8_t> executable;
  std::vector<std::uint8_t> initialized_reads;
  std::vector<FlowReadCompletion> read_completions;
  std::vector<std::uint8_t> original_effects;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Sparse normal-value equations over this exact projected graph. Successful
// writes and unchanged original evaluation intervals generate domains;
// throwing paths retain the previous state. Static
// definition identity never equates values across executions or scope reentry.
// Optional effect facts enable joint executable-completion/value propagation.
// They belong to this exact graph; the solver owns its changing domain span.
// Without them this remains the independent fixed-graph domain analysis.
// Initialization clauses, truth/nullish predicates and raw-proof invalidation
// share edge activation. Truth widening wakes dependents even if kinds do not.
// Valid raw proofs intersect current domains; withdrawing them replays indexed
// inputs. Values widen, initialization joins, and executable edges only activate.
[[nodiscard]] std::variant<FlowNormalKinds,TreeStructureError> AnalyzeFlowNormalKinds(
    const FunctionTree&,const RecoveryFlow&,const FlowLiveness&,
    const RecoveryExpressionInput&,analysis::SemanticBudget = {},
    const FlowEffectInput* completion = nullptr,const FlowInitializationProgram* initialization = nullptr);
[[nodiscard]] std::variant<FlowNormalKinds,TreeStructureError> AnalyzeFlowNormalKinds(
    const FunctionTree&,const RecoveryFlow&,const FlowValueStates&,
    const RecoveryExpressionInput&,analysis::SemanticBudget = {},
    const FlowEffectInput* completion = nullptr,const FlowInitializationProgram* initialization = nullptr);
}  // namespace abcd::source
#endif
