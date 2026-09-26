#ifndef ABCD_ANALYSIS_VALUE_IR_H_
#define ABCD_ANALYSIS_VALUE_IR_H_

#include "abcd/abc/frame_layout.h"
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
  kCallee, kReceiver, kConstructor, kArgument
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
struct OpResult { OpId op; };
struct PhiDefinition { WorkBlockId block; };
struct ValueDefinition {
  std::variant<EntryDefinition, OpResult, PhiDefinition> origin;
  ValueKinds domain;
  std::optional<UseId> first_use;
  ValueLife life = ValueLife::kLive;
};
struct MethodEntryKey {};
using IncomingKey = std::variant<MethodEntryKey, EdgeId>;
using ValueOperand = std::variant<ValueId, SpecialValue, IntegerLiteral, Float64Bits>;
struct InputRead {
  InputSiteId site;
  std::uint16_t ordinal;
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
struct ValueUnary { UnaryOperator op; bool operator==(const ValueUnary&) const = default; };
struct ValueBinary { BinaryOperator op; bool operator==(const ValueBinary&) const = default; };
struct ValueNamedRead { abc::PoolIndex name; bool operator==(const ValueNamedRead&) const = default; };
struct ValueNamedWrite { abc::PoolIndex name; bool operator==(const ValueNamedWrite&) const = default; };
struct ValuePropertyRead { bool operator==(const ValuePropertyRead&) const = default; };
struct ValueBranch { UnaryOperator predicate; bool operator==(const ValueBranch&) const = default; };
struct ValueJump { bool operator==(const ValueJump&) const = default; };
struct ValueReturn { bool operator==(const ValueReturn&) const = default; };
struct ValueNop { bool operator==(const ValueNop&) const = default; };
struct ValueString { abc::PoolIndex string; bool operator==(const ValueString&) const = default; };
struct ValueGuard { std::optional<abc::PoolIndex> name; bool operator==(const ValueGuard&) const = default; };
struct ValueArrayAllocation { bool operator==(const ValueArrayAllocation&) const = default; };
enum class InvokeReceiver : std::uint8_t { kUndefined, kExplicit };
struct ValueInvoke {
  InvokeReceiver receiver;
  std::uint32_t arguments;
  bool operator==(const ValueInvoke&) const = default;
};
// Constructor is also newTarget for these admitted NEWOBJRANGE operations.
struct ValueConstruct {
  std::uint32_t arguments;
  bool operator==(const ValueConstruct&) const = default;
};
using ValueOperation = std::variant<ValueCopy, ValueUnary, ValueBinary,
    ValueNamedRead, ValueNamedWrite, ValuePropertyRead, ValueBranch, ValueJump,
    ValueReturn, ValueNop, ValueString, ValueGuard, ValueArrayAllocation,
    ValueInvoke, ValueConstruct>;
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
// No handlers in normal import. Each throwing operation has its own exit;
// its normal result is unavailable on this exit, even within one basic block.
struct ValueExceptionalExit {
  OpId op;
  OriginId origin;
  LanguageEffect before_throw;
};
struct InputCorrespondence {
  std::vector<std::optional<OpId>> sites;
  std::vector<ValueId> values;
};
// All arrays are owned. IDs never point into source body/SSA or vector storage.
// Input identity and proof envelopes are supplied by the import boundary;
// this mutable graph alone is not a source/publication certificate.
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
