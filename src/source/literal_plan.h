#ifndef ABCD_SOURCE_LITERAL_PLAN_H_
#define ABCD_SOURCE_LITERAL_PLAN_H_

#include "abcd/source/evaluation_plan_cause.h"
#include "abcd/analysis/value_ir.h"
#include <span>
#include "array_initialization.h"

namespace abcd::source {
// A computed object-literal member: the name-setting store
// (STOWNBYVALUEWITHNAMESET) or a CreateDataProperty definition
// (DEFINEFIELDBYVALUE). Literal planning proves each one's evaluation order.
inline bool IsComputedLiteralDefinition(const analysis::ValueOperation& operation) {
  const auto* own = std::get_if<analysis::ValueComputedOwnWrite>(&operation);
  return own && (own->name_setting == analysis::PropertyNameSetting::kPropertyKey ||
                 own->kind == analysis::OwnWriteKind::kDefineField);
}
struct EvaluationPlan;
struct CellEvaluation;
// Typed receiver sources in a freshly verified, simplified owner graph. The
// ABI snapshot and frame read retain their own origins; this predicate never
// equates their SSA identities, replays a frame read, or proves a derived/arrow
// receiver initialized. Frame reads need the original Language-domain proof.
inline bool IsReceiverOperand(const analysis::ValueProgram& graph, const analysis::ValueOperand& operand) {
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  if (!value || value->value >= graph.values.size()) return false;
  const auto& definition = graph.values[value->value];
  if (definition.life != analysis::ValueLife::kLive || !definition.domain.IsLanguage()) return false;
  if (const auto* entry = std::get_if<analysis::EntryDefinition>(&definition.origin))
    return std::holds_alternative<analysis::ReceiverEntry>(*entry);
  const auto* origin = std::get_if<analysis::OpResult>(&definition.origin);
  if (!origin || origin->op.value >= graph.ops.size()) return false;
  const auto& producer = graph.ops[origin->op.value];
  return producer.life == analysis::ValueLife::kLive && producer.result == *value &&
      std::holds_alternative<analysis::ValueFrameThis>(producer.operation);
}
// A direct initialized receiver field is an ordered definition, not a fresh
// object-literal property. Syntax must preserve DefineOwnProperty semantics.
inline bool IsReceiverField(const analysis::ValueProgram& graph, const analysis::ValueOp& op) {
  const auto* field = std::get_if<analysis::ValueOwnWrite>(&op.operation);
  if (!graph.phases.empty() || !field || field->kind != analysis::OwnWriteKind::kDefineField ||
      op.use_count != 2 || op.first_use.value >= graph.uses.size()) return false;
  const auto& base = graph.uses[op.first_use.value];
  return base.role == analysis::OperandRole::kBase && IsReceiverOperand(graph, base.operand);
}
enum class LiteralForm : std::uint8_t { kExpression, kStatements };
struct ObjectInit {
  analysis::OpId allocation;
  std::uint32_t first;
  std::uint32_t count;
  LiteralForm form = LiteralForm::kExpression;
  // Original indexed own commits, not permission for source indexed Set.
  // A complete native selection/aggregate projection is required to publish.
  std::optional<std::uint32_t> indexed_first;
};
struct LiteralPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::optional<ArrayInitializationPlan> arrays;
  std::vector<ObjectInit> objects;
  std::vector<analysis::OpId> definitions;
  std::vector<ArrayInitializationStep> indexed_commits;
  std::vector<std::uint8_t> staged;
  // Per nested allocation/value expression, the unique immediate consumer use.
  // Operand trees preserve initialization/operand/property order; module guards are paired. Empty
  // for roots. Normal blocks or unique phase corridors retain unobserved
  // nesting; phased effects also retain dispatch and staged catch values.
  std::vector<std::optional<analysis::UseId>> nested;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
inline bool IsStagedLiteral(const LiteralPlan* plan, analysis::OpId id) {
  return plan && id.value < plan->staged.size() && plan->staged[id.value];
}

// Initial domain: fresh ordinary object with named definitions, or fresh array
// with dense indexed definitions 0..N-1, or increasing buffered-array writes,
// with available RHSs across normal CFGs, or late owned trees in a local/phase
// interval. Shared all-path effect identities prove normal available-RHS
// execution count/order; exact copy/phi aliases retain identity. The local late
// interval is an implementation gap, not a language invariant.
// Buffered extent/gaps are checked with constants;
// no observation until initialization completes. RHS snapshots precede allocation;
// owned late reads/calls/constructions/closures preserve operand and property effect order. A member
// call's duplicate receiver wire use is consumed by its exact callee Reference;
// CallPlan and tree operand verification independently check that pairing.
// A closure retains its creation environment and child/capture obligations;
// literal ownership neither executes its body nor proves its cells immutable.
// Named name-setting definitions require a uniquely owned fresh creation after
// allocation; their anonymous runtime name is checked with the real child tree.
// Only those obligations seed class-heritage/prototype composition. An absent
// heritage consumes the closed Hole protocol, never a fabricated JS value.
// Staged buffered objects instead preserve allocation and write sites in a normal
// DAG, or a verified activation-local acyclic unfinished region in a cyclic CFG.
// No escape precedes the final write; tree construction independently proves
// every assigned key already denotes a writable own buffer data property.
// module_guards must be the unchanged IndexModuleEvaluations peer index;
// standalone calls without it cannot fold late guarded module reads.
// class_projection must be independently proved by ProjectClassMembers; it
// excludes class member installations from ordinary object-literal ownership.
// receiver_guards is the unchanged derived/captured receiver protocol proof.
// Only its initialized checks can lie inside a native initializer interval;
// the caller independently discharges their receiver origins and keeps PC anchors.
// cells carries the common completed-store/owner proof. Both construction and
// verification freshly check it before ignoring initialized checks in use/effect
// censuses. A Language domain or caller placement alone is insufficient.
// Direct receiver fields stay ordered statement roots and are excluded from
// literal ownership. Not AST/publication permission: preserve snapshots and sites.
[[nodiscard]] std::variant<LiteralPlan, LiteralError> BuildLiteralPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {},
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> class_projection = {},
    std::span<const analysis::OpId> receiver_guards = {},
    const CellEvaluation* cells = nullptr, EvaluationInputSite* failure = nullptr,
    SourceContract contract = kSourceContract);
// Checks supplied groups against use chains and fresh flow, without regrouping.
[[nodiscard]] std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan,
    analysis::SemanticBudget budget = {},
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> class_projection = {},
    std::span<const analysis::OpId> receiver_guards = {},
    const CellEvaluation* cells = nullptr, EvaluationInputSite* failure = nullptr,
    SourceContract contract = kSourceContract);
// Same initializer model across unique normal phase corridors. The original
// phase graph stays intact; copy aliases must have their independent proof.
// No observations (including exceptional snapshots) before the last define,
// RHS snapshots precede allocation; checked single-consumer nesting retains
// operand/property order and the outer allocation's catch-copy sources. A
// nonthrowing allocation instead anchors to its first real throwing initializer,
// only without catch staging. TreeFlow separately discharges original lexical
// try/handler ownership using independently observed AST expression scopes.
// Publication additionally requires the versioned resource-exhaustion exclusion.
// module_guards has the same checked-index contract as the normal-flow API;
// evaluation construction and verification each supply their own fresh index.
[[nodiscard]] std::variant<LiteralPlan, LiteralError> BuildPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {},
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> projected = {}, EvaluationInputSite* failure = nullptr);
[[nodiscard]] std::variant<std::uint64_t, LiteralError> VerifyPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan,
    const EvaluationPlan& evaluation, analysis::SemanticBudget budget = {},
    std::span<const std::optional<analysis::OpId>> module_guards = {},
    std::span<const std::optional<analysis::OpId>> projected = {}, EvaluationInputSite* failure = nullptr);
}  // namespace abcd::source
#endif
