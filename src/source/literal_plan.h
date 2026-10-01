#ifndef ABCD_SOURCE_LITERAL_PLAN_H_
#define ABCD_SOURCE_LITERAL_PLAN_H_

#include "abcd/analysis/value_ir.h"
#include <span>

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
enum class LiteralForm : std::uint8_t { kExpression, kStatements };
struct ObjectInit {
  analysis::OpId allocation;
  std::uint32_t first;
  std::uint32_t count;
  LiteralForm form = LiteralForm::kExpression;
};
struct LiteralPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ObjectInit> objects;
  std::vector<analysis::OpId> definitions;
  std::vector<std::uint8_t> staged;
  // Per nested allocation/value expression, the unique immediate consumer use.
  // Operand trees preserve initialization/operand/property order; module guards are paired. Empty
  // for roots. Only normal, block-local, unobserved nesting is admitted.
  std::vector<std::optional<analysis::UseId>> nested;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
inline bool IsStagedLiteral(const LiteralPlan* plan, analysis::OpId id) {
  return plan && id.value < plan->staged.size() && plan->staged[id.value];
}
enum class LiteralError : std::uint8_t {
  kRevision, kFlow, kDefinition, kReceiver, kObserved, kLateValue, kDomain,
  kCoverage, kResourceLimit, kWorkLimit
};
// Initial domain: fresh ordinary object with named definitions, or fresh array
// with dense indexed definitions 0..N-1, or increasing buffered-array writes,
// in the same block/corridor. Buffered extent/gaps are checked with constants;
// no observation until initialization completes. RHS snapshots precede allocation;
// owned late reads/calls/constructions preserve operand and property effect order. A member
// call's duplicate receiver wire use is consumed by its exact callee Reference;
// CallPlan and tree operand verification independently check that pairing.
// Staged buffered objects instead preserve allocation and write sites in a normal
// DAG. No escape precedes the final write; tree construction independently proves
// every assigned key already denotes a writable own buffer data property.
// module_guards must be the unchanged IndexModuleEvaluations peer index;
// standalone calls without it cannot fold late guarded module reads.
// class_projection must be independently proved by ProjectClassMembers; it
// excludes class member installations from ordinary object-literal ownership.
// Direct receiver fields stay ordered statement roots and are excluded from
// literal ownership. Not AST/publication permission: preserve snapshots and sites.
[[nodiscard]] std::variant<LiteralPlan, LiteralError> BuildLiteralPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> class_projection = {});
// Checks supplied groups against use chains and fresh flow, without regrouping.
[[nodiscard]] std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan,
    analysis::SemanticBudget budget = {},
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> class_projection = {});
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
