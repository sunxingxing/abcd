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
  // Occurrence initialization proof from the shared flow bootstrap. An empty
  // span keeps the binding-invariant compatibility entry; no per-read proof
  // is inferred by the value solver. Existing read substitutions are values.
  std::span<const std::uint8_t> completed_reads;
  std::span<const std::optional<PrimitiveId>> read_values;
  std::span<const std::optional<FlowPrimitiveValue>> invariants;
  std::span<const std::optional<ExpressionTransport>> transports;
  std::span<const std::optional<SymbolId>> read_sources;
  [[nodiscard]] std::size_t size() const {return base.size();}
  [[nodiscard]] FlowValueEquation operator[](std::size_t) const;
};
using FlowDefinitionValue=std::variant<UnknownFlowValue,FlowPrimitiveValue,FlowBindingValue,FlowExpressionValue,FlowCapturedValue>;
struct FlowReadConstant {ExprId expression;PrimitiveId primitive;};
struct FlowReadBinding {ExprId expression;SymbolId binding;};
struct FlowConstants {
  std::vector<std::optional<FlowPrimitiveValue>> values;
  // All-definition invariants grounded in a proven immutable body-entry value.
  // These are read values, never storage mappings or deletion permissions.
  std::vector<std::optional<SymbolId>> bindings;
  std::vector<FlowReadConstant> reads;
  // Completed occurrence values may differ across reads of one binding.
  // Roots are still proven immutable entry identities, never writable slots.
  std::vector<FlowReadBinding> binding_reads;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// All retained successful definitions agree, except dead lexical undefined
// initialization. Include otherwise dead writes: a normal-result binding may
// observe a just-written value without a separate read (e.g. prefix update).
// Sparse monotone value equations need a grounded primitive or immutable entry; unresolved inputs
// become unknown before publication, so copy cycles cannot invent a value.
// Observable cells/entry demand remain unknown. No write/effect removal is
// authorized by this read-substitution fact. Inputs share this exact flow.
// Completed private read occurrences additionally join shared live incoming
// facts; binding invariants remain an independent stronger proof. Normal values
// never authorize early PutValue, lifetime changes or executable-edge removal.
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
