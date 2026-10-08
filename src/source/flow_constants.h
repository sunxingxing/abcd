#ifndef ABCD_SOURCE_FLOW_CONSTANTS_H_
#define ABCD_SOURCE_FLOW_CONSTANTS_H_
#include "flow_liveness.h"
namespace abcd::source {
struct FlowPrimitiveValue {TreeLiteral value;std::optional<PrimitiveId> literal;};
struct FlowBindingValue {SymbolId binding;};
struct UnknownFlowValue {};
struct FlowExpressionValue {ExprId expression;};
struct FlowPrimitiveReference {PrimitiveId primitive;};
struct FlowChoiceValue {ExprId condition,consequent,alternate;};
struct FlowLogicalValue {TreeLogical operation;ExprId left,right;};
// The source semantic adapter publishes normal-result equations once. Effects,
// availability, lexical completion and stored-result identity stay independent.
using FlowValueEquation=std::variant<UnknownFlowValue,FlowPrimitiveReference,FlowBindingValue,
    FlowExpressionValue,FlowChoiceValue,FlowLogicalValue>;
// Borrow one unchanged projection during synchronous analysis. Binding values
// are a compact snapshot; replacements and base instruction meaning stay owned
// by preparation. No E-sized copy of the projected equations is necessary.
struct FlowValueEquations {
  std::span<const FlowValueEquation> base;
  std::span<const std::optional<ExprId>> replacements;
  std::span<const FlowValueEquation> bindings;
  [[nodiscard]] std::size_t size() const {return base.size();}
  [[nodiscard]] FlowValueEquation operator[](std::size_t) const;
};
using FlowDefinitionValue=std::variant<UnknownFlowValue,FlowPrimitiveValue,FlowBindingValue,FlowExpressionValue>;
struct FlowConstants {
  std::vector<std::optional<FlowPrimitiveValue>> values;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// All retained successful definitions agree, except dead lexical undefined
// initialization. Include otherwise dead writes: a normal-result binding may
// observe a just-written value without a separate read (e.g. prefix update).
// Sparse monotone value equations need a grounded primitive; unresolved inputs
// become unknown before publication, so copy cycles cannot invent a value.
// Observable cells/entry demand remain unknown. No write/effect removal is
// authorized by this read-substitution fact. Inputs share this exact flow.
[[nodiscard]] std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow&,const FlowLiveness&,std::span<const FlowDefinitionValue>,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow&,const FlowLiveness&,std::span<const FlowDefinitionValue>,
    std::span<const FlowValueEquation>,std::span<const TreeLiteral>,
    std::span<const std::u16string>,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<FlowConstants,TreeStructureError> AnalyzeFlowConstants(
    const RecoveryFlow&,const FlowLiveness&,std::span<const FlowDefinitionValue>,
    FlowValueEquations,std::span<const TreeLiteral>,std::span<const std::u16string>,
    analysis::SemanticBudget = {});
}
#endif
