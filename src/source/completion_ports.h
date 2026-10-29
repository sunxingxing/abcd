#ifndef ABCD_SOURCE_COMPLETION_PORTS_H_
#define ABCD_SOURCE_COMPLETION_PORTS_H_

#include "exception_control.h"
#include "for_of_scope.h"
#include "abcd/source/for_of_failure.h"
#include "abcd/source/tree_failure.h"

namespace abcd::source {
enum class CompletionPortError : std::uint8_t {
  kInput, kEntry, kCoverage, kPayload, kActivation, kProtection, kOrder,
  kPriority, kInterference, kTarget, kCorrespondence, kResourceLimit, kWorkLimit
};
struct CompletionPortFailure {
  CompletionPortError kind;
  std::optional<analysis::CompletionEdge> edge;
  std::optional<analysis::TryId> region;
  std::variant<std::monostate, CloseCompletionFailure, ExceptionControlError,
      ExceptionControlFailure, ExceptionControlRegionFailure, ForOfIterationError> cause;
  std::optional<TreeLocation> location;
  std::optional<ForOfScopeWitness> placement;
  std::optional<SinglePassFailure> single_pass;
};
struct ProtectionContextId {
  std::uint32_t value;
  auto operator<=>(const ProtectionContextId&) const = default;
};
struct CompletionProtection {
  std::optional<ProtectionContextId> parent;
  std::optional<analysis::CatchScope> scope;
  bool operator==(const CompletionProtection& b) const {
    return parent == b.parent && scope.has_value() == b.scope.has_value() &&
        (!scope || (scope->region == b.scope->region && scope->part == b.scope->part));
  }
};
enum class CompletionPortDisposition : std::uint8_t {
  kAction, kContinuation, kExcludedComparison, kInfeasible
};
struct CompletionPortEntry {
  IteratorProtocolId protocol;
  analysis::OpId read;
  std::uint32_t entry;
  analysis::CompletionEntry original;
  analysis::ExceptionNodeId origin;
  std::optional<analysis::ExceptionNodeId> catcher;
  CompletionPortDisposition disposition=CompletionPortDisposition::kAction;
  bool operator==(const CompletionPortEntry& b) const {
    return protocol == b.protocol && read == b.read && entry == b.entry &&
        original.edge == b.original.edge && original.pending == b.original.pending &&
        original.binding == b.original.binding && origin == b.origin && catcher == b.catcher && disposition==b.disposition;
  }
};
struct CompletionPortExit {
  IteratorProtocolId protocol;
  analysis::OpId read;
  analysis::CloseOutcomeLeaving original;
  ProtectionContextId source, target;
  // Same original edge, not a guessed lexical parent or a terminal Return.
  std::optional<std::uint32_t> next_entry;
  // The current throw operand (if explicit) remains distinct from an
  // internally caught close error and from the pending return payload.
  std::optional<analysis::SsaValueId> binding;
  std::uint32_t entry=0;
  bool operator==(const CompletionPortExit&) const = default;
};
struct CompletionResumePort {
  analysis::TryId region;
  analysis::CompletionEdge original;
  analysis::ExceptionNodeId target;
  ProtectionContextId source_context, target_context;
  bool operator==(const CompletionResumePort&) const = default;
};
struct CompletionPortMember {
  analysis::ExceptionNodeId node;
  IteratorProtocolId protocol;
  analysis::OpId read;
  CloseMemberRole role;
  auto operator<=>(const CompletionPortMember&) const = default;
};
struct CompletionRangeMember {
  analysis::InstructionId instruction;
  analysis::ExceptionNodeId node;
  auto operator<=>(const CompletionRangeMember&) const = default;
};
enum class CompletionDefinitionKind : std::uint8_t { kEntry, kPhi, kCommit, kCatch };
struct CompletionPayloadDefinition {
  analysis::SsaValueId value;
  CompletionDefinitionKind kind;
  std::optional<analysis::ExceptionNodeId> point;
  bool operator==(const CompletionPayloadDefinition&) const = default;
};
struct CompletionPayloadCopy {
  analysis::SsaValueId value, source;
  analysis::ExceptionNodeId commit;
  bool operator==(const CompletionPayloadCopy&) const = default;
};
struct CompletionPayloadPhi {
  analysis::SsaValueId value, source;
  analysis::CompletionEdge edge;
  bool operator==(const CompletionPayloadPhi&) const = default;
};
using CompletionPayloadTransfer=std::variant<CompletionPayloadCopy,CompletionPayloadPhi>;
struct CompletionGate {
  IteratorProtocolId protocol;
  analysis::OpId read;
  analysis::ExceptionNodeId predicate;
  std::optional<analysis::ExceptionNodeId> write;
  std::optional<analysis::SsaValueId> definition;
  abc::RegisterId slot;
  bool operator==(const CompletionGate&) const = default;
};
struct CompletionClosedNode {
  std::uint32_t left=0,right=0;
  bool operator==(const CompletionClosedNode&) const = default;
};
struct CompletionQualifiedPort {
  std::uint32_t entry, closed;
  bool executes;
  std::uint32_t first, count;
  bool operator==(const CompletionQualifiedPort&) const = default;
};
struct CompletionQualifiedTransition {
  std::uint32_t exit;
  std::optional<std::uint32_t> next;
  bool operator==(const CompletionQualifiedTransition&) const = default;
};
// Local outcome proofs and original edge identity establish transitions.
// This certificate grants no source placement. Control planning owns the
// single initiating native action and its enclosing iterator route.
struct CompletionPorts {
  abc::EntityOffset method;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  analysis::TryId refused_region;
  std::array<ExceptionExitEvidence, 2> refused_exits;
  std::optional<ExceptionRegionEvidence> activation;
  std::vector<CompletionProtection> protection;
  std::vector<ProtectionContextId> contexts;
  std::vector<CompletionPortEntry> entries;  // original edge order
  std::vector<CompletionPortExit> exits;
  std::vector<CompletionResumePort> resumes;
  std::vector<CompletionPortMember> members;
  std::vector<CompletionRangeMember> range_members;
  std::vector<CompletionPayloadDefinition> definitions;
  std::vector<CompletionPayloadTransfer> payloads;
  std::vector<std::uint32_t> first_exit, order;
  // Persistent closed-iterator membership retains the activation across raw
  // handler re-entry. Physical environments/SSA values are never equated.
  std::vector<CompletionGate> gates;
  std::vector<CompletionClosedNode> closed;
  std::vector<CompletionQualifiedPort> qualified;
  std::vector<CompletionQualifiedTransition> transitions;
  std::vector<std::optional<std::uint32_t>> roots;
  std::vector<std::optional<IteratorProtocolId>> wrappers;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Legacy-first caller supplies the actual typed refusal. Rebuilds all local
// close facts; a diagnostic ownership flag is never used as admission.
std::optional<CompletionPortError> CompletionPortLimit(const CompletionPortFailure&);
std::variant<CompletionPorts, CompletionPortFailure> DeriveCompletionPorts(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const IteratorKeys&,
    const IteratorProtocolScan&, const ExceptionControlRegionFailure&,
    analysis::SemanticBudget = {}, SourceContract = kSourceContract);
std::variant<std::uint64_t, CompletionPortFailure> VerifyCompletionPorts(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const IteratorKeys&,
    const IteratorProtocolScan&, const CompletionPorts&,
    analysis::SemanticBudget = {}, SourceContract = kSourceContract);
bool CompletionPortOwns(const CompletionPorts&,analysis::ExceptionNodeId,IteratorProtocolId);
}  // namespace abcd::source
#endif
