#ifndef ABCD_SOURCE_ORIGINAL_FLOW_H_
#define ABCD_SOURCE_ORIGINAL_FLOW_H_
#include "tree_checks.h"
#include "abcd/analysis/value_ir.h"
#include "truth_domains.h"
#include <memory>
namespace abcd::source {
struct TreeFlow;
struct OriginalResultDefinition {analysis::OpId operation;analysis::ValueId value;};
struct OriginalPhiDefinition {analysis::ValueId value;analysis::IncomingKey incoming;};
struct OriginalSavedDefinition {
  analysis::ValueOperand operand;
  std::variant<analysis::OpId,analysis::IncomingKey> placement;
};
struct OriginalCatchDefinition {analysis::ValueId value;analysis::WorkBlockId handler;};
// The verified catch clause owns this raw Catch definition. Its block names
// the exception-phase entry; its source body allocates a unit on each visit.
struct OriginalCatchBinding {analysis::ValueId value;analysis::WorkBlockId handler;StmtId body;};
// These are checked source commits of raw results/contributions, not dynamic
// SSA value equality. Stage/cycle saves preserve an operand; phi copies supply
// an incoming contribution; a catch commit publishes the staged contribution.
struct OriginalSourceDefinition {
  std::variant<OriginalResultDefinition,OriginalPhiDefinition,
      OriginalSavedDefinition,OriginalCatchDefinition> origin;
  SymbolId binding;
  ExprId input;
};
enum class OriginalAbruptKind : std::uint8_t {kReturn,kThrow};
enum class OriginalControlKind : std::uint8_t {kPredicate,kTruthSelection,kNullishSelection};
// A checked control input occurrence, not the selected value or an executable
// operation on the enclosing expression. Folded pure tests keep this semantic
// relation without lending their Boolean domains to the selection's result.
struct OriginalControlInput {ExprId expression;OriginalControlKind kind;std::optional<PureTruthTest> test;};
// Original operations retain phase effects and success-only results, without
// copying opcode variants or interpreting them a second time in recovery.
struct OriginalOperation {
  analysis::WorkBlockId block;
  analysis::UseId first_use;
  std::uint32_t use_count;
  std::optional<analysis::ValueId> result;
  analysis::LanguageEffect normal, before_throw;
  analysis::ExceptionalCompletion exceptional;
  analysis::ValueLife life;
  analysis::OriginId origin;
  std::optional<ExprId> expression;
  std::optional<StmtId> statement;
  // Published by operand verification only when the source value equals this
  // result. A shared origin/control anchor is not a result identity: native
  // selections can cover a Boolean conversion while returning an object.
  std::optional<ExprId> result_expression;
  // Raw operation effects bound this native intrinsic action. The source
  // adapter applies the bound only to evaluation, never to separate operand,
  // reference-preparation or declarative-writeback phases.
  std::optional<ExprId> intrinsic_expression;
  // Flow verification owns the native statement; operand verification owns
  // its completion payload. This is not the returned/thrown value's action.
  std::optional<OriginalAbruptKind> abrupt;
  std::optional<OriginalControlInput> control;
};
// Operand evidence stays occurrence-specific. Never join it with operation
// result domains: predicates and completion operands can share source anchors.
struct OriginalInput {
  ExprId expression;
  analysis::ValueOperand operand, resolved;
  std::optional<analysis::UseId> occurrence;
};
struct OriginalInputRange {std::size_t first=0,count=0;};
// Owned by the same immutable prepared tree/flow. A conditional input product,
// never a certificate for an arbitrary mutated tree. Source-only initializers,
// binding activations and recovery projections are added by the common adapter.
struct OriginalFlow {
  RouteInputIdentity identity;
  abc::EntityOffset method;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::size_t statements=0, bindings=0;
  std::vector<analysis::ValueDefinition> values;
  std::vector<analysis::OperandUse> uses;
  std::vector<OriginalOperation> operations;
  std::vector<analysis::WorkBlock> blocks;
  std::vector<analysis::WorkEdge> edges;
  // Produced by control verification at the actual decision boundary. Phased
  // branch successors belong to Commit, not to the operation's Before block.
  std::vector<std::optional<analysis::OpId>> edge_controls; // EdgeId indexed.
  std::vector<analysis::ValueExceptionalExit> exceptional_exits;
  std::vector<analysis::ValuePhaseSite> phases;
  std::vector<analysis::ValueOrigin> origins;
  std::vector<OriginalInput> inputs;
  std::vector<OriginalInputRange> input_ranges;
  std::vector<std::optional<OriginalSourceDefinition>> definitions; // ExprId indexed.
  // Declaration commits have their own identity. The initializer denotes a
  // captured input, not the declarative write or its binding activation.
  std::vector<std::optional<OriginalSourceDefinition>> declarations; // StmtId indexed.
  std::vector<std::optional<OriginalCatchBinding>> catches; // SymbolId indexed.
};
[[nodiscard]] std::shared_ptr<OriginalFlow> RetainOriginalFlow(
    const analysis::ValueProgram&,const FunctionTree&,TreeFlow&,TreeCheckBudget&);
void RecordOriginalInput(OriginalFlow&,const analysis::ValueOperand&,
    const analysis::ValueOperand&,ExprId,std::optional<analysis::UseId>,TreeCheckBudget&);
void IndexOriginalInputs(OriginalFlow&,TreeCheckBudget&);
void CheckOriginalFlow(const FunctionTree&,const OriginalFlow&);
struct OriginalResult {
  analysis::ValueId value;
  ExprId expression;
  analysis::ValueKinds kinds;
  std::optional<analysis::WorkBlockId> commit;
};
// One value-identity query for inline and stored consumers. Missing projection
// means no raw certificate, not failure of the current language expression.
[[nodiscard]] std::optional<OriginalResult> FindOriginalResult(const OriginalFlow&,analysis::OpId);
// Raw domain lookup only. Recovery decisions use the RecoveryFlow overload,
// which also requires current success/catch/control correspondence and shares
// that obligation with inline consumers. This query cannot certify a trial.
[[nodiscard]] analysis::ValueKinds OriginalDefinitionKinds(const OriginalFlow&,const OriginalSourceDefinition&);
}  // namespace abcd::source
#endif
