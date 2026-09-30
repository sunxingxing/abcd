#ifndef ABCD_SOURCE_SOURCE_TREE_H_
#define ABCD_SOURCE_SOURCE_TREE_H_

#include <string>
#include <optional>
#include <variant>
#include <vector>

#include "abcd/analysis/value_ir.h"
#include "abcd/source/source_route.h"

namespace abcd::source {
struct ExprId { std::uint32_t value = 0; auto operator<=>(const ExprId&) const = default; };
struct PropertyId { std::uint32_t value = 0; auto operator<=>(const PropertyId&) const = default; };
struct StmtId { std::uint32_t value = 0; auto operator<=>(const StmtId&) const = default; };
struct SymbolId { std::uint32_t value = 0; auto operator<=>(const SymbolId&) const = default; };
struct ParameterId { std::uint32_t value = 0; auto operator<=>(const ParameterId&) const = default; };
struct ScopeId { std::uint32_t value = 0; auto operator<=>(const ScopeId&) const = default; };
struct NameId { std::uint32_t value = 0; auto operator<=>(const NameId&) const = default; };
struct TextId { std::uint32_t value = 0; auto operator<=>(const TextId&) const = default; };
struct TreeOriginId { std::uint32_t value = 0; auto operator<=>(const TreeOriginId&) const = default; };
struct AnchorId { std::uint32_t value = 0; auto operator<=>(const AnchorId&) const = default; };
struct FunctionOwnerId { std::uint32_t value = 0; auto operator<=>(const FunctionOwnerId&) const = default; };
struct ModuleOwnerId { std::uint32_t value = 0; auto operator<=>(const ModuleOwnerId&) const = default; };
enum class CallableKind : std::uint8_t {
  kFunction, kMethod, kArrow, kBaseConstructor, kDerivedConstructor,
  kAsyncFunction, kAsyncMethod, kAsyncArrow, kGenerator, kGeneratorMethod, kAsyncGenerator
};
constexpr bool IsArrowCallable(CallableKind kind) {
  return kind == CallableKind::kArrow || kind == CallableKind::kAsyncArrow;
}
// Native named function expressions provide immutable lexical self bindings.
// Methods do not; arrows use a separately checked creation-time binding.
constexpr bool AllowsNamedSelf(CallableKind kind) {
  return kind == CallableKind::kFunction || kind == CallableKind::kAsyncFunction ||
         kind == CallableKind::kGenerator || kind == CallableKind::kAsyncGenerator;
}
constexpr bool IsAsyncCallable(CallableKind kind) {
  return kind == CallableKind::kAsyncFunction || kind == CallableKind::kAsyncMethod || kind == CallableKind::kAsyncArrow ||
      kind == CallableKind::kAsyncGenerator;
}
constexpr bool IsGeneratorCallable(CallableKind kind) {
  return kind == CallableKind::kGenerator || kind == CallableKind::kGeneratorMethod || kind == CallableKind::kAsyncGenerator;
}
constexpr bool IsMethodCallable(CallableKind kind) {
  return kind == CallableKind::kMethod || kind == CallableKind::kAsyncMethod || kind == CallableKind::kGeneratorMethod;
}
enum class Strictness : std::uint8_t { kStrict, kSloppy };
enum class ScopeKind : std::uint8_t { kParameters, kFunctionBody, kBlock, kCaptures, kModule, kFunctionSelf, kCatch };
enum class BindingKind : std::uint8_t { kParameter, kLocal, kFunctionSelf, kCompilerTemporary, kCapture, kModule, kCatchParameter };
enum class SelfForm : std::uint8_t { kNamedFunction, kCreationBinding };
// Preserve the ABC callable kind. A method carrier supplies an arrow owner's
// native invocation only after receiver/new.target observation is excluded.
enum class InvocationForm : std::uint8_t { kDeclared, kArrowMethod };
enum class NameEvidence : std::uint8_t { kMetadata, kDebugRange, kSynthesized };
enum class DeclarationKind : std::uint8_t { kParameter, kLet, kConst, kCatchParameter };
enum class TemporaryReason : std::uint8_t {
  kMultipleUses, kEffectSnapshot, kPhiJoin, kParallelCopyCycle,
  kCrossRegionValue, kReferenceSnapshot, kUnprovedInline, kPredicateSnapshot,
  kExceptionSnapshot, kPendingCompletion, kInternalSentinel
};
struct TreeScope {
  ScopeKind kind;
  std::optional<ScopeId> parent;
};
struct TreeSymbol {
  ScopeId scope;
  NameId name;
  BindingKind kind;
  DeclarationKind declaration;
  NameEvidence evidence;
  std::optional<TemporaryReason> temporary;
};
struct TreeParameter {
  SymbolId symbol;
  std::optional<ExprId> initializer;
  bool rest = false;
};
struct CapturedSymbol {
  FunctionOwnerId owner;
  SymbolId symbol;
  bool operator==(const CapturedSymbol&) const = default;
};
// A local AST reference to an enclosing source binding, not a declaration or
// copied scalar. A family/creation check must validate target and spelling.
struct TreeCapture { SymbolId symbol; CapturedSymbol target; };
struct ModuleSymbol {
  ModuleOwnerId owner;
  SymbolId symbol;
  auto operator<=>(const ModuleSymbol&) const = default;
};
enum class ModuleAccess : std::uint8_t { kReadOnly, kMutable };
// Alias to an enclosing source module symbol, not an ABC slot, declaration or
// snapshot. ReadOnly forbids assignment through an import; its value stays live.
// A record proof must bind owner/target/name/access before source publication.
struct TreeModuleAlias {
  SymbolId symbol;
  ModuleSymbol target;
  ModuleAccess access;
  // Owned by this module initializer; its declaration occurs at the first
  // original store. Imported/enclosing aliases never become local declarations.
  bool local = false;
};
struct UndefinedLiteral {};
struct NullLiteral {};
struct BooleanLiteral { bool value; };
struct IntegerLiteral { std::int32_t value; };
struct NumberLiteral { std::uint64_t bits; };
using TreeLiteral = std::variant<UndefinedLiteral, NullLiteral, BooleanLiteral,
                                 IntegerLiteral, NumberLiteral, TextId>;
struct SymbolExpr { SymbolId symbol; };
// Free global-name lookup, not a lexical Symbol or globalThis property read.
// Publication additionally requires an unshadowed enclosing source context.
struct GlobalNameExpr { NameId name; };
// Original realm identity; the publication destination supplies its binding.
struct RealmObjectExpr {};
struct SymbolConstructorExpr {};
struct ThisExpr { FunctionOwnerId owner; };
struct ComputedKey { ExprId expression; };
using MemberKey = std::variant<NameId, ComputedKey>;
struct MemberExpr { ExprId base; MemberKey key; };
enum class TreeUnary : std::uint8_t { kNot, kNegate, kBitNot, kPlus, kTypeof };
// Unary operands are values, not unresolved References. In particular typeof
// must perform GetValue (including a possible global lookup failure) first.
struct UnaryExpr { TreeUnary operation; ExprId operand; };
struct AwaitExpr { ExprId operand; };
enum class YieldForm : std::uint8_t { kDirect, kDelegate };
struct YieldExpr { ExprId operand; YieldForm form = YieldForm::kDirect; };
struct BinaryExpr { analysis::BinaryOperator operation; ExprId left; ExprId right; };
enum class TreeLogical : std::uint8_t { kAnd, kOr };
struct LogicalExpr { TreeLogical operation; ExprId left; ExprId right; };
struct SymbolTarget { SymbolId symbol; };
struct MemberTarget { ExprId base; MemberKey key; };
using AssignmentTarget = std::variant<SymbolTarget, MemberTarget>;
struct AssignmentExpr { AssignmentTarget target; ExprId value; };
// Each argument slot and expression occurrence has exactly one syntax owner.
// Call references are explicit: value calls must not acquire member this or
// direct-eval semantics merely because their callee prints as an identifier.
struct ArgumentRange { std::uint32_t first; std::uint32_t count; };
enum class CallReference : std::uint8_t { kValue, kMember };
struct CallExpr { ExprId callee; ArgumentRange arguments; CallReference reference; };
struct ConstructExpr { ExprId constructor; ArgumentRange arguments; };
// Fresh pinned arguments object. Slots are conditional current-value leaves;
// count and extra tail belong to this owner's unexposed native invocation.
// Publication requires an explicit exact factory, not a mutable global helper.
struct ArgumentSnapshotExpr { FunctionOwnerId owner; ArgumentRange slots; };
// Fresh creation of an owned child function, not a function value constant.
// The family must contain and verify that child before publication. No raw
// method/pool/register ID is an executable operand in this node.
// Capture edges are lexical references checked at creation, not evaluated JS
// arguments. Moving this node requires a separate capture-lifetime proof.
struct CaptureRange { std::uint32_t first; std::uint32_t count; };
struct FunctionExpr { FunctionOwnerId child; CaptureRange captures; };
// Fresh empty aggregates are expression occurrences, never TreeLiteral values.
// Buffered elements, descriptors and own-defines need a separate initializer
// proof before being folded into either node.
struct EmptyArrayExpr {};
struct EmptyObjectExpr {};
struct PropertyRange { std::uint32_t first; std::uint32_t count; };
enum class AggregateKind : std::uint8_t { kObject, kArray };
struct ObjectExpr { PropertyRange properties; AggregateKind kind = AggregateKind::kObject; };
// Computed string data property, never a colon-style __proto__ setter or method.
// Array aggregates reuse the ordered property arena with canonical decimal keys;
// keys must equal element ordinals and are implicit in native array syntax.
struct DataProperty { NameId key; ExprId value; TreeOriginId origin; };
using TreeExpression = std::variant<TreeLiteral, SymbolExpr, GlobalNameExpr, RealmObjectExpr,
    SymbolConstructorExpr, ThisExpr, MemberExpr,
                                    UnaryExpr, AwaitExpr, YieldExpr, BinaryExpr, LogicalExpr, AssignmentExpr,
                                    CallExpr, ConstructExpr, EmptyArrayExpr, EmptyObjectExpr, ObjectExpr, FunctionExpr,
                                    ArgumentSnapshotExpr>;
struct ExprNode { TreeExpression expression; TreeOriginId origin; };
struct BlockStmt { ScopeId scope; std::uint32_t first_child; std::uint32_t child_count; };
struct DeclarationStmt { SymbolId symbol; std::optional<ExprId> initializer; };
struct ExpressionStmt { ExprId expression; };
struct IfStmt { ExprId condition; StmtId consequent; std::optional<StmtId> alternate; };
struct ReturnStmt { std::optional<ExprId> value; };
struct ThrowStmt { ExprId value; };
struct CatchClause { SymbolId parameter; StmtId body; };
// All regions own block statements. The handler's catch scope alone binds its
// parameter; protected body/finalizer remain siblings. Syntax is not proof of
// ABC handler coverage or finally completion preservation (T09).
struct TryStmt {
  StmtId body;
  std::optional<CatchClause> handler;
  std::optional<StmtId> finalizer;
};
// Canonical loop syntax: while (true), with the original header predicate in
// the body. This keeps header effects and edge copies at their actual sites.
struct LabelId { std::uint32_t value; auto operator<=>(const LabelId&) const = default; };
struct LoopStmt { StmtId body; std::optional<LabelId> label; };
// Targets are enclosing syntax owners, never CFG/region IDs. Outer transfers
// carry a preassigned label; the printer must not infer or retarget a jump.
struct BreakStmt { StmtId target; std::optional<LabelId> label; };
struct ContinueStmt { StmtId target; std::optional<LabelId> label; };
struct DebuggerStmt {};
enum class RecoveryMode : std::uint8_t { kCompleteOnly, kAllowPartial };
struct RecoveryIssueId { std::uint32_t value = 0; auto operator<=>(const RecoveryIssueId&) const = default; };
enum class RecoveryReason : std::uint8_t {
  kUnsupportedOperation, kUnsupportedControl, kUnavailableValue, kUnresolvedBinding,
  kIncompleteChild
};
struct RecoveryIssue {
  RecoveryReason reason;
  std::optional<abc::CodeOffset> begin;
  std::optional<abc::CodeOffset> end;
};
// A readable gap, never an executable stand-in for an unknown operation.
// Builders must omit dependent statements rather than invent values across it.
struct UnrecoveredStmt { RecoveryIssueId issue; };
using TreeStatement = std::variant<BlockStmt, DeclarationStmt, ExpressionStmt, IfStmt, ReturnStmt,
                                   LoopStmt, BreakStmt, ContinueStmt, DebuggerStmt, ThrowStmt, TryStmt,
                                   UnrecoveredStmt>;
struct StmtNode { TreeStatement statement; TreeOriginId origin; };

// Origins/validation side tables may refer to working IDs. Executable node
// operands above never do. Combined origins share references, not PC copies.
struct OriginalSite {
  analysis::InputSiteId site;
  abc::CodeOffset pc;
  std::optional<abc::CodeOffset> original = {};
};
// Part ranges partition origin_parts in origin order. Parts reference earlier
// origins; sharing is through IDs, not overlapping adjacency-list ranges.
struct CombinedOrigin { std::uint32_t first_part; std::uint32_t part_count; };
struct ElidedOrigin {
  analysis::InputSiteId site;
  abc::CodeOffset pc;
  std::optional<abc::CodeOffset> original = {};
};
struct EdgeOrigin { analysis::EdgeId edge; };
struct EntryOrigin {};
// Structural loop syntax has no additional instruction PC. Header identity is
// provenance only; executable loop and transfer operands remain syntax IDs.
struct LoopOrigin { analysis::WorkBlockId header; };
struct TryOrigin { analysis::TryId region; };
struct BindingOrigin { SymbolId symbol; };
using TreeOrigin = std::variant<OriginalSite, CombinedOrigin, ElidedOrigin, EdgeOrigin, BindingOrigin, LoopOrigin, EntryOrigin, TryOrigin>;
struct BlockBoundary {
  StmtId block;
  // A zero-width position before child[index], or after all children at count.
  // Internal CFG jumps cannot be mapped to the enclosing block's exit token.
  std::uint32_t index;
};
struct FunctionExit {};
using AnchorPosition = std::variant<BlockBoundary, FunctionExit>;
struct TreeAnchor { AnchorPosition position; TreeOriginId origin; };
struct OpPlacement {
  analysis::OpId operation;
  std::variant<ExprId, StmtId, AnchorId, PropertyId> node;
};
// Prologue operations moved to complete formal syntax, not body instructions.
// This side table requires a default-signature proof before publication.
struct SignaturePlacement {
  analysis::OpId operation;
  ParameterId parameter;
  TreeOriginId origin;
};
// Correspondence for a pending-return/rethrow cleanup projection. Executable
// behavior lives in ordinary AST nodes; the printer does not read this table.
struct FinalizerComposition {
  analysis::TryId region;
  StmtId enclosure;
};
enum class YieldCleanupPlacement : std::uint8_t { kBranchArm, kFollowing };
struct YieldReturnComposition {
  analysis::OpId yield;
  StmtId enclosure, branch;
  std::optional<StmtId> completion;
  YieldCleanupPlacement cleanup;
  // A preserving terminal nested in a tail-only Block/If chain is moved out
  // of native finally; falling through that chain preserves caller payload.
  std::optional<StmtId> completion_block;
};
struct FunctionTree {
  RouteInputIdentity identity;
  abc::EntityOffset method;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  FunctionOwnerId owner;
  CallableKind callable;
  Strictness strictness;
  std::optional<SymbolId> self;
  ScopeId parameter_scope;
  ScopeId body_scope;
  std::optional<ScopeId> capture_scope;
  StmtId body;
  std::vector<TreeParameter> parameters;
  std::vector<TreeCapture> captures;
  std::vector<TreeScope> scopes;
  std::vector<TreeSymbol> symbols;
  std::vector<std::u16string> names;
  std::vector<std::u16string> texts;
  std::vector<ExprNode> expressions;
  std::vector<DataProperty> properties;
  std::vector<ExprId> arguments;
  // Implicit lexical references owned by FunctionExpr, not runtime arguments.
  // The child capture table must match these before a family can be published.
  std::vector<SymbolId> closure_captures;
  std::vector<StmtNode> statements;
  std::vector<StmtId> children;
  std::vector<TreeOrigin> origins;
  std::vector<TreeOriginId> origin_parts;
  std::vector<TreeAnchor> anchors;
  std::vector<OpPlacement> operations;
  std::vector<SignaturePlacement> signature;
  std::vector<RecoveryIssue> recovery_issues;
  std::optional<ScopeId> module_scope;
  std::vector<TreeModuleAlias> modules;
  // Creation-proven runtime name. Without self, computed data-property
  // NamedEvaluation preserves it without depending on a mutable intrinsic.
  // NamedFunction self uses this spelling; CreationBinding keeps it distinct
  // from the private const and uses the same computed naming boundary.
  // Empty strings are names too; absence is missing evidence, not an empty name.
  std::optional<NameId> inferred_name;
  // Both forms bind this exact owner immutably. CreationBinding uses a private
  // creation-time const, separate from the target's runtime name and body.
  SelfForm self_form = SelfForm::kNamedFunction;
  InvocationForm invocation = InvocationForm::kDeclared;
  // LabelId -> loop statement, function-local and separate from variable names.
  std::vector<StmtId> labels;
  std::vector<FinalizerComposition> finalizers;
  std::vector<YieldReturnComposition> yield_returns;
  // Invocation-private identity used only by a closed VM Hole protocol.
  std::optional<SymbolId> hole;
};
// FunctionTree is the direct printer input, not a source-publication token.
// Structural safety checks do not imply complete recovery or IR equivalence.
struct TreeStorage { std::size_t bytes; std::uint64_t work; };
enum class TreeStorageError : std::uint8_t { kResourceLimit, kWorkLimit };
// Capacity-based owned storage accounting, including UTF-16 name/literal
// payloads. No allocation and no traversal of expression graphs. SSO payloads
// are conservatively counted twice; this is not whole-process RSS evidence.
[[nodiscard]] std::variant<TreeStorage, TreeStorageError> MeasureTreeStorage(
    const FunctionTree& tree, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
