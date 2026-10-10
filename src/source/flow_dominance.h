#ifndef ABCD_SOURCE_FLOW_DOMINANCE_H_
#define ABCD_SOURCE_FLOW_DOMINANCE_H_
#include "recovery_flow.h"
#include "flow_liveness.h"
#include "abcd/analysis/dominators.h"
#include <algorithm>
namespace abcd::source {
struct FlowDominance {
  analysis::DominatorTree tree;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
  [[nodiscard]] bool Dominates(FlowPoint before,FlowPoint after) const {
    return tree.Dominates({before.value},{after.value});
  }
};
// Include EVERY completion edge, including exceptions and conservative cleanup.
// CSR adjacency feeds the existing LT implementation directly. Arbitrary
// fanout needs no synthetic events and never becomes a recovery admission.
[[nodiscard]] std::variant<FlowDominance,TreeStructureError> AnalyzeFlowDominance(
    const RecoveryFlow&,analysis::SemanticBudget = {});
struct FlowInitializationId {std::uint32_t value;auto operator<=>(const FlowInitializationId&)const=default;};
enum class FlowInitializationValue : std::uint8_t {kUninitialized,kInitialized};
using FlowInitializationInput=std::variant<FlowInitializationValue,FlowInitializationId>;
struct FlowInitializationClause {
  FlowInitializationId target;
  FlowInitializationInput input;
  std::optional<std::size_t> completion;
};
struct FlowInitializationQuery {LiveBinding fact;FlowInitializationInput input;};
// Shared reset-aware equations. Completion indices refer to this unchanged
// conservative graph; consumers may activate them, never infer lexical facts
// again from statement shapes. Queries are sorted by LiveBinding.
struct FlowInitializationProgram {
  const RecoveryFlow* owner=nullptr;
  std::size_t events=0,completions=0,states=0;
  std::vector<FlowInitializationClause> clauses;
  std::vector<FlowInitializationQuery> queries;
  std::size_t retained_bytes=0,allocated_bytes=0;
  std::uint64_t work=0;
};
struct FlowInitializations {
  FlowDominance order;
  std::vector<std::optional<FlowPoint>> declarations;
  // Sparse demanded states, after successful events. Implicit reads precede
  // allocation/initialization; ordinary read events do not change this state.
  std::vector<LiveBinding> completed;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
  // Source references/definitions/inputs are included automatically; callers
  // request additional candidate states explicitly. Missing query precision is
  // conservative, never a single old declaration's dominance certificate.
  // Unreachable retained syntax has no execution-order demand; lexical name
  // resolution is still checked independently for that occurrence.
  [[nodiscard]] bool Completed(SymbolId binding,FlowPoint occurrence) const {
    if(!order.tree.nodes[occurrence.value].begin)return true;
    return std::binary_search(completed.begin(),completed.end(),LiveBinding{occurrence,binding});
  }
};
[[nodiscard]] std::variant<FlowInitializations,TreeStructureError> AnalyzeFlowInitializations(
    const RecoveryFlow&,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<FlowInitializations,TreeStructureError> AnalyzeFlowInitializations(
    const RecoveryFlow&,std::span<const LiveBinding> queries,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<FlowInitializationProgram,TreeStructureError> BuildFlowInitializationProgram(
    const RecoveryFlow&,analysis::SemanticBudget = {});
}
#endif
