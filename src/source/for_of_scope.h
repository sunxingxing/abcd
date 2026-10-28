#ifndef ABCD_SOURCE_FOR_OF_SCOPE_H_
#define ABCD_SOURCE_FOR_OF_SCOPE_H_
#include "abcd/source/for_of_failure.h"
#include "control_skeleton.h"
#include "for_of_identity.h"
namespace abcd::source {
enum class ForOfScopeWitnessKind : std::uint8_t {
  kMissingNaturalLoop,
  kBorrowedHeader
};
struct ForOfScopeWitness {
  ForOfScopeWitnessKind kind;
  ForOfIterationError cause;
  IteratorProtocolId protocol;
  analysis::OpId acquisition, advance;
  std::optional<LoopId> borrowed;
  bool operator==(const ForOfScopeWitness&) const = default;
};
enum class SinglePassError : std::uint8_t {
  kInput,
  kActivation,
  kEntry,
  kRepeat,
  kExit,
  kParent,
  kProtection,
  kPayload,
  kCoverage,
  kCorrespondence,
  kResourceLimit,
  kWorkLimit
};
struct SinglePassFailure {
  SinglePassError kind;
  std::optional<IteratorProtocolId> protocol;
  std::optional<analysis::CompletionEdge> edge;
};
struct NaturalLoopScope {
  LoopId loop;
  bool operator==(const NaturalLoopScope&) const = default;
};
struct SinglePassIteratorScope {
  IteratorProtocolId protocol;
  analysis::OpId acquisition, advance, decision, value;
  analysis::ExceptionNodeId entry, decision_commit, value_commit, body,
      continuation, exit;
  analysis::CompletionEdge exhausted;
  std::optional<IteratorProtocolId> enclosing;
  bool operator==(const SinglePassIteratorScope&) const = default;
};
using ForOfScope = std::variant<NaturalLoopScope, SinglePassIteratorScope>;
struct SinglePassMember {
  IteratorProtocolId protocol;
  analysis::ExceptionNodeId node;
  auto operator<=>(const SinglePassMember&) const = default;
};
struct SinglePassCut {
  IteratorProtocolId protocol;
  analysis::CompletionEdge edge;
  analysis::PendingCompletion pending;
  bool operator==(const SinglePassCut&) const = default;
};
struct SinglePassStepExit {
  IteratorProtocolId protocol;
  analysis::CompletionEdge edge;
  analysis::ExceptionNodeId target;
  bool operator==(const SinglePassStepExit&) const = default;
};
struct SinglePassProtection {
  std::optional<analysis::TryId> region;
  analysis::CatchPart part = analysis::CatchPart::kProtected;
  bool operator==(const SinglePassProtection&) const = default;
};
// Logical activations never acquire a physical LoopId. Original cut edges,
// coverage, dead nominal repeats and dispatch remain independently replayable.
struct SinglePassScopes {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  ForOfScopeWitness activation;
  std::vector<std::optional<SinglePassIteratorScope>> scopes;
  std::vector<std::optional<IteratorProtocolId>> parents;
  std::vector<SinglePassMember> coverage;
  std::vector<SinglePassCut> cuts;
  std::vector<SinglePassStepExit> step_exits;
  std::vector<analysis::CompletionEdge> dead_entries;
  std::vector<SinglePassProtection> protection, parents_protection;
  std::vector<std::optional<analysis::TryId>> dispatch;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
std::variant<SinglePassScopes, SinglePassFailure> ProveSinglePassScopes(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&,
    const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    const ForOfScopeWitness&, analysis::SemanticBudget = {});
std::variant<SinglePassScopes, SinglePassFailure> RebuildSinglePassScopes(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&,
    const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    const ForOfScopeWitness&, analysis::SemanticBudget = {});
std::variant<std::uint64_t, SinglePassFailure> VerifySinglePassScopes(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&,
    const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    const SinglePassScopes&, analysis::SemanticBudget = {});
struct OriginalFlow;
struct OriginalIterations;
struct RecoveryFlow;
class TreeCheckBudget;
void VerifySinglePassRecovery(const OriginalFlow&, const OriginalIterations&,
                              const RecoveryFlow&, TreeCheckBudget&);
void PlaceSinglePassScopes(const analysis::ValueProgram&, ExceptionControl&,
                           const SinglePassScopes&, ControlQuota&);
}  // namespace abcd::source
#endif
