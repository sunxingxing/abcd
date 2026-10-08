#ifndef ABCD_SOURCE_FLOW_CONSTANTS_H_
#define ABCD_SOURCE_FLOW_CONSTANTS_H_
#include "flow_liveness.h"
namespace abcd::source {
struct FlowPrimitiveValue {TreeLiteral value;std::optional<ExprId> literal;};
struct FlowBindingValue {SymbolId binding;};
struct UnknownFlowValue {};
using FlowDefinitionValue=std::variant<UnknownFlowValue,FlowPrimitiveValue,FlowBindingValue>;
struct FlowConstants {
  std::vector<std::optional<FlowPrimitiveValue>> values;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// All retained successful definitions agree, except dead lexical undefined
// initialization. Include otherwise dead writes: a normal-result binding may
// observe a just-written value without a separate read (e.g. prefix update).
// SCC copy equations need a grounded primitive; cycles cannot invent a value.
// Observable cells/entry demand remain unknown. No write/effect removal is
// authorized by this read-substitution fact. Inputs share this exact flow.
[[nodiscard]] std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow&,const FlowLiveness&,std::span<const FlowDefinitionValue>,analysis::SemanticBudget = {});
}
#endif
