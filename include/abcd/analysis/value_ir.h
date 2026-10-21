#ifndef ABCD_ANALYSIS_VALUE_IR_H_
#define ABCD_ANALYSIS_VALUE_IR_H_

#include "abcd/abc/frame_layout.h"
#include "abcd/analysis/exception_phase.h"
#include "abcd/analysis/exception_dispatch.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_kinds.h"

namespace abcd::analysis {
struct ValueId {
  std::uint32_t value = 0;
  auto operator<=>(const ValueId&) const = default;
};
struct OpId {
  std::uint32_t value = 0;
  auto operator<=>(const OpId&) const = default;
};
struct UseId {
  std::uint32_t value = 0;
  auto operator<=>(const UseId&) const = default;
};
struct WorkBlockId {
  std::uint32_t value = 0;
  auto operator<=>(const WorkBlockId&) const = default;
};
struct EdgeId {
  std::uint32_t value = 0;
  auto operator<=>(const EdgeId&) const = default;
};
struct InputSiteId {
  std::uint32_t value = 0;
  auto operator<=>(const InputSiteId&) const = default;
};
struct OriginId {
  std::uint32_t value = 0;
  auto operator<=>(const OriginId&) const = default;
};
struct IrRevision {
  std::uint64_t value = 0;
  auto operator<=>(const IrRevision&) const = default;
};
struct CfgRevision {
  std::uint64_t value = 0;
  auto operator<=>(const CfgRevision&) const = default;
};
enum class OperandRole : std::uint8_t {
  kInput, kBase, kKey, kStoredValue, kLeft, kRight, kCondition,
  kReturnValue, kGuardValue, kDiagnosticName, kPhiInput,
  kCallee, kReceiver, kConstructor, kArgument, kHomeObject, kHeritage, kThrownValue, kGetter, kSetter, kNameFlag,
  kDestination, kIndex, kSource
};
enum class ReadPurpose : std::uint8_t { kOperand, kProtocol };
enum class ValueLife : std::uint8_t { kLive, kRemoved };
enum class OriginDisposition : std::uint8_t { kRetained, kElidedPure, kCombined };
struct ValueOrigin {
  InputSiteId site;
  abc::CodeOffset pc;
  std::optional<abc::CodeOffset> original;
  OriginDisposition disposition = OriginDisposition::kRetained;
};
struct ParameterEntry { std::uint32_t index; };
struct ReceiverEntry {};
struct CallableEntry {};
struct NewTargetEntry {};
// An unproved VM slot is not a language local initialized to undefined.
struct UnknownEntry { ScalarSlot slot; };
using EntryDefinition = std::variant<ParameterEntry, ReceiverEntry,
    CallableEntry, NewTargetEntry, UnknownEntry>;
// Normal graphs define after op; phased graphs define only at Commit entry.
struct OpResult { OpId op; std::optional<WorkBlockId> commit; };
struct PhiDefinition { WorkBlockId block; };
struct CatchDefinition { WorkBlockId block; };
struct ValueDefinition {
  std::variant<EntryDefinition, OpResult, PhiDefinition, CatchDefinition> origin;
  ValueKinds domain;
  std::optional<UseId> first_use;
  ValueLife life = ValueLife::kLive;
};
struct MethodEntryKey {};
using IncomingKey = std::variant<MethodEntryKey, EdgeId>;
using ValueOperand = std::variant<ValueId, SpecialValue, IntegerLiteral, Float64Bits>;
struct InputRead {
  InputSiteId site;
  std::uint32_t ordinal;
};
struct OperandUse {
  std::variant<OpId, ValueId> owner;  // ValueId owns a phi input.
  OperandRole role;
  ReadPurpose purpose;
  ValueOperand operand;
  std::optional<IncomingKey> incoming;
  std::optional<InputRead> input;
  std::optional<UseId> previous;
  std::optional<UseId> next;
  ValueLife life = ValueLife::kLive;
};
struct ValueCopy { bool operator==(const ValueCopy&) const = default; };
// Only owner-normalized AwaitValue imports here; raw VM state/resume opcodes
// remain unsupported. Keep the protocol PCs for operand/origin correspondence.
struct ValueAwait { AwaitRegion origin; bool operator==(const ValueAwait&) const = default; };
struct ValueYield { YieldRegion origin; bool operator==(const ValueYield&) const = default; };
struct ValueUnary { UnaryOperator op; bool operator==(const ValueUnary&) const = default; };
struct ValueBinary { BinaryOperator op; bool operator==(const ValueBinary&) const = default; };
struct ValueComputedOwnWrite { PropertyNameSetting name_setting = PropertyNameSetting::kNone; bool operator==(const ValueComputedOwnWrite&) const = default; };
struct ValueArraySpread { bool operator==(const ValueArraySpread&) const = default; };
struct ValueNamedRead { abc::PoolIndex name; PropertyLookup lookup = PropertyLookup::kObject; bool operator==(const ValueNamedRead&) const = default; };
struct ValueNamedWrite { abc::PoolIndex name; bool operator==(const ValueNamedWrite&) const = default; };
// Own definition is not [[Set]]. Keep the pinned operation family and IC slot
// even when both forms target a named property; source equivalence needs an
// allocation/descriptor proof, not just a syntactically valid assignment.
struct ValueOwnWrite {
  OwnWriteKind kind;
  abc::PoolIndex name;
  std::uint16_t cache_slot;
  PropertyNameSetting name_setting = PropertyNameSetting::kNone;
  bool operator==(const ValueOwnWrite&) const = default;
};
// STOWNBYINDEX defines an own property, preserving the accumulator. The
// immediate is an index (not a pool slot); wide encoding has no IC operand.
struct ValueIndexedWrite {
  std::uint32_t index;
  std::optional<std::uint16_t> cache_slot;
  IndexedDefinition kind = IndexedDefinition::kOwnStore;
  bool operator==(const ValueIndexedWrite&) const = default;
};
// DEFINEGETTERSETTERBYVALUE retains five ordered inputs and its accumulator
// result. Name assignment is a protocol flag, never descriptor enumerability.
// Native class folding needs separate ownership/descriptor evidence.
struct ValueDefineAccessor { bool operator==(const ValueDefineAccessor&) const = default; };
enum class EnumerationAction : std::uint8_t { kAcquire, kNext };
struct ValueEnumeration {
  EnumerationAction action;
  bool operator==(const ValueEnumeration&) const = default;
};
// Raw acquisition retains pinned semantics, including non-object success.
struct ValueIteratorAcquire { bool operator==(const ValueIteratorAcquire&) const = default; };
struct ValueObjectCheck { bool operator==(const ValueObjectCheck&) const = default; };
struct ValuePropertyDelete { bool operator==(const ValuePropertyDelete&) const = default; };
struct ValuePropertyRead { PropertyLookup lookup = PropertyLookup::kObject; bool operator==(const ValuePropertyRead&) const = default; };
struct ValueBranch { UnaryOperator predicate; bool operator==(const ValueBranch&) const = default; };
struct ValueJump { bool operator==(const ValueJump&) const = default; };
struct ValueReturn { bool operator==(const ValueReturn&) const = default; };
struct ValueThrow { bool operator==(const ValueThrow&) const = default; };
struct ValueNop { bool operator==(const ValueNop&) const = default; };
struct ValueDebugger { bool operator==(const ValueDebugger&) const = default; };
struct ValueFrameThis { bool operator==(const ValueFrameThis&) const = default; };
struct ValueFrameFunction { bool operator==(const ValueFrameFunction&) const = default; };
// Realm identity, never a property/name lookup of mutable globalThis.
struct ValueGlobalObject { bool operator==(const ValueGlobalObject&) const = default; };
struct ValueSymbolConstructor { bool operator==(const ValueSymbolConstructor&) const = default; };
// slots counts declared conditional inputs, NOT actual invocation arguments.
// Actual count/tail come from ValueProgram::arguments; every execution allocates.
// COPYRESTARGS creates a fresh argument-tail array. Its physical ABI slots
// are not source parameter bindings; native ...rest substitution needs the
// original method prologue check in syntax resolution.
struct ValueRestArguments {
  std::uint16_t first_argument = 0;
  bool operator==(const ValueRestArguments&) const = default;
};
struct ValueArgumentSnapshot {
  std::uint32_t slots;
  bool operator==(const ValueArgumentSnapshot&) const = default;
};
struct ValueString { abc::PoolIndex string; PoolTextKind kind = PoolTextKind::kString; bool operator==(const ValueString&) const = default; };
// Name lookup and IC-dependent var lookup have different domains. Retain the
// opcode contract; neither denotes a source lexical declaration or pure read.
enum class GlobalLookup : std::uint8_t { kName, kVar };
struct ValueGlobalRead {
  GlobalLookup lookup;
  abc::PoolIndex name;
  bool operator==(const ValueGlobalRead&) const = default;
};
struct ValueGuard { std::optional<abc::PoolIndex> name; bool operator==(const ValueGuard&) const = default; };
// Live module-cell read, not a pool/string/request index or a frozen imported
// value. Source binding and TDZ equivalence require the enclosing module proof.
struct ValueModuleRead {
  ModuleSpace space;
  ModuleSlot slot;
  ModuleLoading loading = ModuleLoading::kOrdinary;
  ModuleDomain domain = ModuleDomain::kOrdinary;
  bool operator==(const ValueModuleRead&) const = default;
};
// Store to this record's live local module cell. The stored-value operand is
// explicit; no accumulator result or permission to emit a declaration follows.
struct ValueModuleWrite {
  ModuleSlot slot;
  bool operator==(const ValueModuleWrite&) const = default;
};
// Strict [[Set]] with base/key/stored-value operands, no accumulator result.
// Own definition, fixed-index runtime operations and initialization are distinct.
struct ValuePropertyWrite {
  bool operator==(const ValuePropertyWrite&) const = default;
};
struct ValueArrayAllocation {
  // A present regional literal slot must be decoded before emitting syntax;
  // buffered arrays are fresh allocations, never empty-array placeholders.
  std::optional<abc::PoolIndex> literal;
  bool operator==(const ValueArrayAllocation&) const = default;
};
struct ValueObjectAllocation {
  // Absent means CREATEEMPTYOBJECT, present is the original regional literal
  // slot. A buffer is not an empty object or a reusable object constant; its
  // properties/methods and captured environment require source-level proof.
  std::optional<abc::PoolIndex> literal;
  bool operator==(const ValueObjectAllocation&) const = default;
};
// RuntimeCreateIterResultObj creates ordinary own data properties in this
// order. Its done flag uses IsTrue, never truthiness or user coercion.
struct ValueIterResult { bool operator==(const ValueIterResult&) const = default; };
// Unresolved creation template, not a reusable function value. The origin site
// binds its environment/module and target profile; source publication still
// requires CreationBinding, CapturePlan and complete child-tree verification.
struct ValueFunctionCreation {
  CreationKind kind;
  abc::PoolIndex method;
  std::uint8_t length;
  HomeObjectInput home_object;
  std::uint16_t cache_slot;
  bool operator==(const ValueFunctionCreation&) const = default;
};
// Each DEFINECLASSWITHBUFFER occurrence creates a fresh constructor/prototype
// pair and captures the current environment/module. The heritage protocol
// operand may be the VM Hole sentinel (no extends), never JS undefined.
// Constructor/member bodies and inheritance still require class-tree binding.
struct ValueClassCreation {
  abc::PoolIndex constructor;
  abc::PoolIndex literal;
  std::uint16_t length;
  std::uint16_t cache_slot;
  abc::ClassKind kind = abc::ClassKind::kOrdinary;
  bool operator==(const ValueClassCreation&) const = default;
};
// Depth/slot stay relative to the ORIGINAL environment state at this origin.
// They are not register aliases, source variable names or dynamic cell IDs.
struct ValueLexicalLoad {
  LexicalAddress address;
  bool operator==(const ValueLexicalLoad& other) const {
    return address.depth.value == other.address.depth.value && address.slot.value == other.address.slot.value;
  }
};
struct ValueLexicalStore {
  LexicalAddress address;
  bool operator==(const ValueLexicalStore& other) const {
    return address.depth.value == other.address.depth.value && address.slot.value == other.address.slot.value;
  }
};
struct ValueEnvironmentPush {
  std::uint16_t slots;
  std::optional<abc::PoolIndex> scope;
  bool operator==(const ValueEnvironmentPush&) const = default;
};
struct ValueEnvironmentPop { bool operator==(const ValueEnvironmentPop&) const = default; };
struct ValueCurrentEnvironment { bool operator==(const ValueCurrentEnvironment&) const = default; };
enum class InvokeReceiver : std::uint8_t { kUndefined, kExplicit };
struct ValueInvoke {
  InvokeReceiver receiver;
  std::uint32_t arguments;
  CallArgumentForm form = CallArgumentForm::kRegisters;
  bool operator==(const ValueInvoke&) const = default;
};
// Constructor is also newTarget for these admitted NEWOBJRANGE operations.
struct ValueConstruct {
  std::uint32_t arguments;
  bool operator==(const ValueConstruct&) const = default;
};
struct ValueConstructApply {
  std::uint16_t profile_slot;
  bool operator==(const ValueConstructApply&) const = default;
};
// Pinned super call: parent and newTarget come from the invocation,
// not from an explicit constructor operand. Only normal completion writes acc;
// initializing/checking this remains a separate constructor protocol obligation.
enum class SuperArgumentSource : std::uint8_t { kExplicit, kInvocationFrame };
struct ValueSuperCall {
  std::uint16_t arguments;
  std::optional<std::uint16_t> cache_slot;
  SuperArgumentSource source = SuperArgumentSource::kExplicit;
  bool operator==(const ValueSuperCall&) const = default;
};
// Explicit current constructor and aggregate argument array remain separate
// uses. Parent/newTarget, array expansion and receiver initialization need the
// spread protocol proof; this is not a range call with a single argument.
struct ValueSuperSpread {
  std::uint8_t cache_slot;
  bool operator==(const ValueSuperSpread&) const = default;
};
// Keep the VM initialization guard until a constructor protocol proof consumes
// it. A normal successor does not itself authorize dropping the exception.
struct ValueSuperCheck {
  SuperCheckKind kind;
  bool operator==(const ValueSuperCheck&) const = default;
};
// VM sentinel, not a JS literal. A source lowering must consume the closed
// protocol before evaluation/printing; never substitute undefined or null.
struct ValueHole { bool operator==(const ValueHole&) const = default; };
struct ValueConcurrentNotification {
  NotificationOwner owner = NotificationOwner::kInvocationFunction;
  bool operator==(const ValueConcurrentNotification&) const = default;
};
using ValueOperation = std::variant<ValueCopy, ValueUnary, ValueBinary, ValueHole, ValueAwait, ValueYield,
    ValueNamedRead, ValueNamedWrite, ValueOwnWrite, ValueIndexedWrite, ValueDefineAccessor, ValuePropertyRead, ValueBranch, ValueJump,
    ValueReturn, ValueNop, ValueString, ValueGlobalRead, ValueGuard, ValueArrayAllocation, ValueObjectAllocation, ValueIterResult,
    ValueInvoke, ValueConstruct, ValueConstructApply, ValueFunctionCreation, ValueClassCreation, ValueLexicalLoad, ValueLexicalStore,
    ValueEnvironmentPush, ValueEnvironmentPop, ValueCurrentEnvironment, ValueModuleRead,
    ValueModuleWrite, ValuePropertyWrite, ValuePropertyDelete, ValueEnumeration, ValueIteratorAcquire, ValueObjectCheck, ValueDebugger, ValueFrameThis, ValueFrameFunction,
    ValueGlobalObject, ValueSymbolConstructor, ValueRestArguments, ValueArgumentSnapshot, ValueThrow, ValueSuperCall, ValueSuperSpread, ValueSuperCheck, ValueArraySpread, ValueComputedOwnWrite, ValueConcurrentNotification>;
struct ValueOp {
  ValueOperation operation;
  WorkBlockId block;
  std::uint32_t order;
  UseId first_use;
  std::uint32_t use_count;
  std::optional<ValueId> result;
  LanguageEffect normal;
  LanguageEffect before_throw;
  ExceptionalCompletion exceptional;
  OriginId origin;
  ValueLife life = ValueLife::kLive;
  ExceptionCause exception_cause = ExceptionCause::kUnclassified;
};
struct WorkEdge {
  WorkBlockId source;
  WorkBlockId target;
  FlowEdgeKind kind;
  std::uint8_t successor;
};
struct WorkBlock {
  OpId first_op;
  std::uint32_t op_count;
  EdgeId first_edge;
  std::uint8_t edge_count;
};
struct ValueHandler {
  DispatchTarget dispatch;
  WorkBlockId entry;
  bool operator==(const ValueHandler&) const = default;
};
// No handlers in normal import. Each throwing operation has its own exit;
// its normal result is unavailable on this exit, even within one basic block.
struct ValueExceptionalExit {
  OpId op;
  OriginId origin;
  LanguageEffect before_throw;
  std::optional<ValueHandler> handler;
};
struct InputCorrespondence {
  std::vector<std::optional<OpId>> sites;
  std::vector<ValueId> values;
};
// All arrays are owned. IDs never point into source body/SSA or vector storage.
// Input identity and proof envelopes are supplied by the import boundary;
// this mutable graph alone is not a source/publication certificate.
enum class PhaseReachability : std::uint8_t { kReachable, kUnreachable };
struct ValuePhaseSite {
  ExceptionNodeId input;
  InputSiteId instruction;
  ExceptionPhase phase;
  std::optional<OpId> operation;
  PhaseReachability reachability = PhaseReachability::kReachable;
};
struct ValueProgram {
  IrRevision revision;
  CfgRevision cfg_revision;
  std::vector<WorkBlock> blocks;
  std::vector<WorkEdge> edges;
  std::vector<ValueOp> ops;
  std::vector<ValueDefinition> values;
  std::vector<OperandUse> uses;
  std::vector<ValueOrigin> origins;
  std::vector<ValueExceptionalExit> exceptional_exits;
  InputCorrespondence input;
  std::optional<ArgumentFrameFact> arguments;
  // Empty for normal graphs; otherwise one site per block. Phase identity is
  // distinct from instruction/PC identity, especially for Catch definitions.
  std::vector<ValuePhaseSite> phases;
};
enum class ValueGraphError : std::uint8_t {
  kUseLink, kUseOwner, kUseRange, kDefinition, kResourceLimit
};
// Structural use-chain verifier only, not dominance/input/effect verification.
// O(values + uses + ops), one byte/use scratch, charged before allocation.
[[nodiscard]] std::optional<ValueGraphError> VerifyValueUses(
    const ValueProgram& program, std::size_t scratch_bytes);
}  // namespace abcd::analysis
#endif
