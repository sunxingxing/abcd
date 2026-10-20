#ifndef ABCD_SOURCE_ORIGINAL_FLOW_H_
#define ABCD_SOURCE_ORIGINAL_FLOW_H_
#include "tree_checks.h"
#include "abcd/analysis/value_ir.h"
#include "truth_domains.h"
#include "cell_plan.h"
#include "member_update.h"
#include "cell_parameter_default.h"
#include "for_in_emission.h"
#include "for_of_emission.h"
#include "abcd/analysis/exception_regions.h"
#include <memory>
namespace abcd::source {
class ClassInnerScopeProof;
enum class ClassInitializationPortKind : std::uint8_t {
  kClassInnerInitialize,
  kStaticInitializeBefore,
  kStaticInitializeCommit,
  kPublicPublish,
  kCompletedClass
};
struct OriginalClassInitializationPort {
  ClassInitializationPortKind kind;
  analysis::OpId operation;
  analysis::OriginId origin;
  std::optional<analysis::UseId> use;
  std::optional<analysis::ValueId> value;
  bool operator==(const OriginalClassInitializationPort&) const = default;
};
struct OriginalClassInitialization {
  ExprId expression;
  std::shared_ptr<const ClassInnerScopeProof> certificate;
  std::vector<OriginalClassInitializationPort> ports;
};
struct TreeFlow;
struct OriginalConcurrentNotification {
  analysis::OpId notification,returned;
  analysis::UseId value;
  StmtId statement;
  AnchorId anchor;
  bool operator==(const OriginalConcurrentNotification&)const=default;
};
// Checked native ports retain raw Commit identities, never fabricated ExprIds.
struct OriginalEnumeration {
  ForInEventPair original;
  StmtId statement;
  ExprId object;
  SymbolId key;
  ScopeId scope;
  EnumerationException acquisition;
  analysis::EdgeId success, exhausted;
};
struct OriginalEnumerationExit {StmtId statement; std::uint32_t loop; LoopTransferKind kind; TreeOriginId origin;};
struct OriginalEnumerationInput { std::uint32_t loop; analysis::UseId use; ExprId expression; };

// Allocation sites identify dynamic cell families, not one scalar value or
// one invocation's unit. The verified declaration and source scope are kept
// separate: scope entry allocates a source unit, declaration initializes it.
struct OriginalCellActivation {
  analysis::OpId operation;
  StmtId marker; // One success marker shared by every cell of this allocation.
  auto operator<=>(const OriginalCellActivation&)const=default;
};
struct OriginalCellBinding {
  // Resolved in environments, after the allocation's successful transition.
  // Consumers no longer reconstruct an EnvironmentId from an allocation OpId.
  analysis::LexicalBinding storage;
  SymbolId binding;
  ScopeId scope;
  std::optional<StmtId> declaration;
  // Independently verified success occurrence, separate from EnvironmentId.
  std::optional<OriginalCellActivation> activation;
};
struct OriginalCellReference {CellId cell;CellAccessKind kind;ExprId expression;};
struct OriginalCells {
  std::vector<OriginalCellBinding> bindings; // CellId indexed.
  std::vector<std::optional<OriginalCellReference>> references; // OpId indexed.
};
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
enum class OriginalStatementKind : std::uint8_t {kNormal,kReturn,kThrow};
enum class OriginalControlKind : std::uint8_t {kPredicate,kTruthSelection,kNullishSelection};
// A checked control input occurrence, not the selected value or an executable
// operation on the enclosing expression. Folded pure tests keep this semantic
// relation without lending their Boolean domains to the selection's result.
struct OriginalControlInput {ExprId expression;OriginalControlKind kind;std::optional<PureTruthTest> test;};
// The verified initialization proof excludes Hole in this captured load value.
// This guard has no native action; later reads of the same cell are not its input.
struct OriginalInitializedGuard {analysis::OpId load;analysis::ValueId value;};
struct OriginalActionRegionId {
  std::uint32_t value;
  auto operator<=>(const OriginalActionRegionId&)const=default;
};
// A verified composition implements ONE native action. Only its entry and
// successful exit have physical source boundaries; internal VM states (such
// as a pre-guard Hole) must never masquerade as completed language values.
// The adapter proves membership/order and the native binding reference once.
enum class OriginalReadKind : std::uint8_t { kModule, kCapture };
struct OriginalActionRegion {
  ExprId expression;
  analysis::OpId entry, exit, result;
  SymbolId reference;
  std::optional<analysis::WorkBlockId> commit;
  OriginalReadKind kind = OriginalReadKind::kModule;
};
using OriginalWriteSite=std::variant<ExprId,StmtId>;
enum class OriginalCompletionBoundary : std::uint8_t { kProjectedResult, kIntrinsicAction, kInvocationBinding, kUnmodeled };
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
  // Creation success precedes evaluating an aggregate's property values.
  // Result identity is retained separately; it cannot move the raw Commit to
  // the end of an enclosing source projection.
  OriginalCompletionBoundary completion = OriginalCompletionBoundary::kProjectedResult;
  // A literal's checked own definition belongs to its property occurrence,
  // not the enclosing allocation or the expression producing the stored value.
  std::optional<PropertyId> intrinsic_property;
  // Native invocation binding creates this value, before body evaluation.
  // Its later read is a value correspondence, never the allocation's physical
  // Before/Commit. The callable owner has verified hoisting/frame ownership.
  std::optional<ParameterId> invocation_parameter;
  // Flow verification owns the native statement; operand verification owns
  // its completion payload. This is not the returned/thrown value's action.
  std::optional<OriginalStatementKind> statement_kind;
  std::optional<OriginalControlInput> control;
  std::optional<OriginalInitializedGuard> initialized_guard;
  // Physical binding write verified at this source commit. A numeric staging
  // save has a definition correspondence but is not the conversion's write.
  std::optional<OriginalWriteSite> write;
  bool forwarding_throw = false;
  std::optional<analysis::EnumerationAction> enumeration;

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
struct OriginalExceptionRegion {
  analysis::InstructionRange body, handler;
};
// Invocation-only input is retained separately from the initialized-formal
// residual. Raw argument and body entry are different value identities.
struct OriginalParameterDefault {
  CellParameterDefault proof;
  ExprId initializer;
  SymbolId parameter, outer;
};
struct OriginalParameterDefaults {
  std::shared_ptr<const analysis::ValueProgram> input;
  std::uint32_t formal_count = 0, length = 0;
  std::vector<OriginalParameterDefault> entries;
};
struct OriginalIterationInput { IteratorProtocolId protocol; analysis::UseId use; ExprId expression; };
// Immutable facts transferred only after fresh TreeFlow and operand checks.
// Protocol results retain VM identity, never an expression standing for an iterator.
struct OriginalCloseMember { IteratorProtocolId protocol; CloseMemberRole role; };
struct OriginalIterationBinding { SymbolId target; ExprId iterable; StmtId body; TreeOriginId origin; };
struct OriginalIterations {
  ForOfEmissionView view;
  ForOfEvents events;
  std::vector<std::optional<OriginalCloseMember>> close_members;
  std::vector<ForOfLoop> loops;
  std::vector<analysis::ValueId> input_values;
  std::vector<ForOfKeyPhi> phis;
  std::vector<OriginalIterationBinding> bindings;
  std::vector<std::optional<ForOfMember>> operations, values;
  std::vector<StmtId> statements;
  std::vector<std::optional<StmtId>> closes;
  std::vector<OriginalIterationInput> inputs;
  std::vector<OriginalEnumerationExit> exits;
};
struct OriginalMemberUpdate { ExprId expression; MemberUpdate proof; MemberMutationExpr syntax; };
struct OriginalFlow {
  std::vector<OriginalMemberUpdate> member_updates;
  std::vector<OriginalClassInitialization> class_initializations;
  std::optional<OriginalIterations> iterations;
  std::optional<OriginalParameterDefaults> parameter_defaults;
  std::shared_ptr<const ParameterExpressionSource> parameter_expressions;
  std::vector<OriginalConcurrentNotification> notifications;
  // Retain exact immutable operations only for concurrent completion recovery.
  // This prevents a cached tree port from inventing a raw notification/return.
  std::vector<analysis::ValueOperation> concurrent_operations;

  RouteInputIdentity identity;
  abc::EntityOffset method;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::size_t statements=0, bindings=0;
  std::vector<OriginalExceptionRegion> exception_regions;
  std::vector<analysis::InactiveExceptionRegion> inactive_regions;
  std::vector<OriginalEnumeration> enumerations;
  std::vector<std::optional<ForInMember>> enumeration_operations, enumeration_values;
  std::vector<std::optional<std::uint32_t>> forwarded_operations, forwarded_blocks;
  std::vector<analysis::ValueId> canonical_values;
  std::vector<OriginalEnumerationInput> enumeration_inputs;
  std::vector<OriginalEnumerationExit> enumeration_exits;
  std::vector<analysis::ValueDefinition> values;
  std::vector<analysis::OperandUse> uses;
  std::vector<OriginalOperation> operations;
  std::vector<OriginalActionRegion> action_regions;
  // Allocated only when compositions exist. Missing membership withdraws the
  // certificate; it cannot fall back to an individual member's effect bound.
  std::vector<std::optional<OriginalActionRegionId>> action_region_members;
  std::vector<analysis::WorkBlock> blocks;
  std::vector<analysis::WorkEdge> edges;
  // Produced by control verification at the actual decision boundary. Phased
  // branch successors belong to Commit, not to the operation's Before block.
  std::vector<std::optional<analysis::OpId>> edge_controls; // EdgeId indexed.
  std::vector<analysis::ValueExceptionalExit> exceptional_exits;
  // Indexed by exceptional exit, absent when all causes are unclassified.
  // This is imported semantic evidence, never inferred from printed source
  // or from an allocation-effect bit which may also accompany user callbacks.
  std::vector<analysis::ExceptionCause> exception_causes;
  std::vector<analysis::ValuePhaseSite> phases;
  std::vector<analysis::ValueOrigin> origins;
  std::vector<OriginalInput> inputs;
  std::vector<OriginalInputRange> input_ranges;
  std::vector<std::optional<OriginalSourceDefinition>> definitions; // ExprId indexed.
  // Declaration commits have their own identity. The initializer denotes a
  // captured input, not the declarative write or its binding activation.
  std::vector<std::optional<OriginalSourceDefinition>> declarations; // StmtId indexed.
  std::vector<std::optional<OriginalCatchBinding>> catches; // SymbolId indexed.
  std::optional<OriginalCells> cells;
  // The exact independently verified CellEvaluation topology. EnvironmentIds
  // stay in this immutable owner; normal_after is never a source allocation
  // point or a proof that a projected binding keeps its dynamic identity.
  std::shared_ptr<const analysis::ValueEnvironments> environments;
};
[[nodiscard]] std::shared_ptr<OriginalFlow> RetainOriginalFlow(
    const analysis::ValueProgram&,const FunctionTree&,TreeFlow&,TreeCheckBudget&);
void RecordOriginalInput(OriginalFlow&,const analysis::ValueOperand&,
    const analysis::ValueOperand&,ExprId,std::optional<analysis::UseId>,TreeCheckBudget&);
void IndexOriginalInputs(OriginalFlow&,TreeCheckBudget&);
void CheckOriginalFlow(const FunctionTree&,const OriginalFlow&);
void VerifyInactiveOriginalRegions(const OriginalFlow&, TreeCheckBudget&);
struct OriginalResult {
  analysis::ValueId value;
  ExprId expression;
  analysis::ValueKinds kinds;
  std::optional<analysis::WorkBlockId> commit;
  // The success boundary of a composed native action may belong to its final
  // guard, rather than to the raw producer of the guarded language value.
  std::optional<analysis::OpId> commit_owner;
};
[[nodiscard]] const OriginalActionRegion* FindOriginalActionRegion(const OriginalFlow&,analysis::OpId);
// One value-identity query for inline and stored consumers. Missing projection
// means no raw certificate, not failure of the current language expression.
[[nodiscard]] std::optional<OriginalResult> FindOriginalResult(const OriginalFlow&,analysis::OpId);
// Raw domain lookup only. Recovery decisions use the RecoveryFlow overload,
// which also requires current success/catch/control correspondence and shares
// that obligation with inline consumers. This query cannot certify a trial.
[[nodiscard]] analysis::ValueKinds OriginalDefinitionKinds(const OriginalFlow&,const OriginalSourceDefinition&);
}  // namespace abcd::source
#endif
