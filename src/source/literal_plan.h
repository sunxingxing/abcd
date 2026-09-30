#ifndef ABCD_SOURCE_LITERAL_PLAN_H_
#define ABCD_SOURCE_LITERAL_PLAN_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct EvaluationPlan;
// A direct initialized receiver field is an ordered definition, not a fresh
// object-literal property. Syntax must preserve DefineOwnProperty semantics.
inline bool IsReceiverField(const analysis::ValueProgram& graph, const analysis::ValueOp& op) {
  const auto* field = std::get_if<analysis::ValueOwnWrite>(&op.operation);
  if (!graph.phases.empty() || !field || field->kind != analysis::OwnWriteKind::kDefineField ||
      op.use_count != 2 || op.first_use.value >= graph.uses.size()) return false;
  const auto& base = graph.uses[op.first_use.value];
  const auto* value = std::get_if<analysis::ValueId>(&base.operand);
  if (base.role != analysis::OperandRole::kBase || !value || value->value >= graph.values.size()) return false;
  const auto* entry = std::get_if<analysis::EntryDefinition>(&graph.values[value->value].origin);
  return entry && std::holds_alternative<analysis::ReceiverEntry>(*entry);
}
struct ObjectInit {
  analysis::OpId allocation;
  std::uint32_t first;
  std::uint32_t count;
};
struct LiteralPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ObjectInit> objects;
  std::vector<analysis::OpId> definitions;
  // Per allocation, the unique enclosing initializer RHS occurrence. Empty
  // for roots. Only normal single-block, unobserved nesting is admitted.
  std::vector<std::optional<analysis::UseId>> nested;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class LiteralError : std::uint8_t {
  kRevision, kFlow, kDefinition, kReceiver, kObserved, kLateValue, kDomain,
  kCoverage, kResourceLimit, kWorkLimit
};
// Initial domain: fresh ordinary object with named definitions, or fresh array
// with dense indexed definitions 0..N-1, in the same block/corridor;
// no observation until initialization completes, RHS available before allocation.
// Direct receiver fields stay ordered statement roots and are excluded from
// literal ownership. Not AST/publication permission: preserve snapshots and sites.
[[nodiscard]] std::variant<LiteralPlan, LiteralError> BuildLiteralPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Checks supplied groups against use chains and fresh flow, without regrouping.
[[nodiscard]] std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan,
    analysis::SemanticBudget budget = {});
// Same initializer model across unique normal phase corridors. The original
// phase graph stays intact; copy aliases must have their independent proof.
// No observations (including exceptional snapshots) before the last define,
// and every RHS must already be available at allocation. Source publication
// additionally requires the versioned resource-exhaustion exclusion.
[[nodiscard]] std::variant<LiteralPlan, LiteralError> BuildPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, LiteralError> VerifyPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
