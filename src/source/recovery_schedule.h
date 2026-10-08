#ifndef ABCD_SOURCE_RECOVERY_SCHEDULE_H_
#define ABCD_SOURCE_RECOVERY_SCHEDULE_H_
#include "tree_checks.h"
#include <initializer_list>
#include <limits>

namespace abcd::source {
enum class RecoveryPass {
  kDeclarations,kConstants,kBindingValues,kAliases,kStoredResults,kDeadDefinitions,kInitialValues,
  kBindings,kSnapshots,kStorage,kInertStatements,kInertOperands,kLoopDeclarations,kDeclarationKinds
};
enum class RecoveryFact : std::uint8_t {kExpressions,kFlow,kLiveness,kDemand,kInitializations,kConstants,kCopies};
class RecoveryFactSet {
 public:
  constexpr RecoveryFactSet(std::initializer_list<RecoveryFact> facts) {
    for(const auto fact:facts)Add(fact);
  }
  constexpr void Add(RecoveryFact fact) {bits_|=std::uint32_t{1}<<static_cast<unsigned>(fact);}
  [[nodiscard]] constexpr bool Contains(RecoveryFact fact) const {
    return (bits_&(std::uint32_t{1}<<static_cast<unsigned>(fact)))!=0;
  }
 private:
  std::uint32_t bits_=0;
};
enum class RecoveryPassScope {kValues,kValueOrStorage,kStorage,kReadValues,kNormalization,kGrammar};
struct RecoveryPassContract {
  RecoveryFactSet inputs;
  RecoveryPassScope scope;
  bool preserves_initialization;
  bool preserves_demand_checkpoint=false;
};
// These contracts describe semantic dependencies, never bytecode/source
// admission. Providers close dependencies lazily; no eager graph rebuild is
// introduced. Initialization preservation must be proved for each pass.
[[nodiscard]] inline RecoveryPassContract PassContract(RecoveryPass pass) {
  using Fact=RecoveryFact;using Scope=RecoveryPassScope;
  switch(pass) {
    case RecoveryPass::kDeclarations:return {{Fact::kFlow},Scope::kValues,false};
    case RecoveryPass::kConstants:return {{Fact::kConstants},Scope::kValues,false};
    case RecoveryPass::kBindingValues:return {{Fact::kConstants,Fact::kCopies},Scope::kReadValues,false};
    case RecoveryPass::kAliases:return {{Fact::kLiveness},Scope::kValueOrStorage,false};
    // Store identity elision retains RHS completion at its occurrence.
    case RecoveryPass::kStoredResults:return {{Fact::kExpressions},Scope::kNormalization,true};
    // Removed definitions/initial values/bindings leave no retained reference
    // to the removed completion; other occurrence initialization is unchanged.
    case RecoveryPass::kDeadDefinitions:case RecoveryPass::kInitialValues:case RecoveryPass::kBindings:
      return {{Fact::kDemand},Scope::kNormalization,true};
    // A snapshot removes its own single definition/read while preserving the
    // RHS at the destination. It cannot make another producer newly dead.
    // Transport only an already established checkpoint, never create one.
    case RecoveryPass::kSnapshots:return {{Fact::kLiveness},Scope::kStorage,false,true};
    // Coalescing proves the new slot initialized before every remapped read.
    case RecoveryPass::kStorage:return {{Fact::kLiveness,Fact::kInitializations},Scope::kStorage,true};
    // Only proven inert occurrences disappear; no retained initialization moves.
    case RecoveryPass::kInertStatements:case RecoveryPass::kInertOperands:
      return {{Fact::kExpressions},Scope::kNormalization,true};
    case RecoveryPass::kLoopDeclarations:return {{},Scope::kGrammar,false};
    // Grammar-only const spelling changes neither values nor completion.
    case RecoveryPass::kDeclarationKinds:return {{Fact::kFlow},Scope::kGrammar,true};
  }
  throw TreeStructureError::kOwnership;
}
[[nodiscard]] inline RecoveryFactSet RequiredFacts(RecoveryPass pass) {
  auto result=PassContract(pass).inputs;
  if(result.Contains(RecoveryFact::kConstants)||result.Contains(RecoveryFact::kCopies))result.Add(RecoveryFact::kLiveness);
  if(result.Contains(RecoveryFact::kLiveness)||result.Contains(RecoveryFact::kDemand)||
      result.Contains(RecoveryFact::kInitializations))result.Add(RecoveryFact::kFlow);
  // Flow and expressions are bootstrapped together: conservative effects ->
  // initialization proof -> refined effects. They are one provider boundary,
  // not mutually scheduled optimization passes.
  if(result.Contains(RecoveryFact::kFlow)||result.Contains(RecoveryFact::kExpressions)) {
    result.Add(RecoveryFact::kFlow);result.Add(RecoveryFact::kExpressions);
  }
  return result;
}
struct ProjectionRevision {std::uint64_t value=0;};
class RecoverySchedule {
 public:
  [[nodiscard]] ProjectionRevision Revision() const {return revision_;}
  void RequireAnalysis() const {RequireTree(phase_!=Phase::kCommit,TreeStructureError::kOwnership);}
  void RequireCurrent(ProjectionRevision revision) const {
    RequireAnalysis();RequireTree(revision.value==revision_.value,TreeStructureError::kOwnership);
  }
  void RequireFact(RecoveryFact fact) const {
    RequireAnalysis();
    if(active_)RequireTree(RequiredFacts(*active_).Contains(fact),TreeStructureError::kOwnership);
    else RequireTree(stage_==Stage::kGrammar&&(fact==RecoveryFact::kExpressions||fact==RecoveryFact::kFlow),TreeStructureError::kOwnership);
  }
  void Start(RecoveryPass pass) {
    RequireIdle();const auto scope=PassContract(pass).scope;
    RequireTree(cleanup_==Cleanup::kIdle||
        (cleanup_==Cleanup::kInitialValues&&pass==RecoveryPass::kInitialValues)||
        (cleanup_==Cleanup::kBindings&&pass==RecoveryPass::kBindings),TreeStructureError::kOwnership);
    if(pass==RecoveryPass::kInitialValues)RequireTree(cleanup_==Cleanup::kInitialValues,TreeStructureError::kOwnership);
    if(pass==RecoveryPass::kBindings)RequireTree(cleanup_==Cleanup::kBindings,TreeStructureError::kOwnership);
    const bool allowed=scope==RecoveryPassScope::kValues?stage_==Stage::kValues:
        scope==RecoveryPassScope::kStorage?stage_==Stage::kStorage:
        scope==RecoveryPassScope::kReadValues?stage_==Stage::kReadValues:
        scope==RecoveryPassScope::kGrammar?stage_==Stage::kGrammar:
        scope==RecoveryPassScope::kNormalization?stage_!=Stage::kGrammar:
        stage_==Stage::kValues||stage_==Stage::kStorage;
    RequireTree(allowed,TreeStructureError::kOwnership);
    // Binding placement and producer coverage both require all independent
    // value/initialization/binding cleanup owners at the current projection.
    if((pass==RecoveryPass::kDeclarations||pass==RecoveryPass::kAliases)&&stage_==Stage::kValues)
      RequireTree(!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    active_=pass;before_=revision_;phase_=Phase::kAnalysis;
  }
  // First commit invalidates all versioned facts before any plan write. Further
  // writes belong to the same atomic batch; reading facts is forbidden until
  // Finish. The caller owns proposal IDs, never borrowed analysis references.
  [[nodiscard]] bool Commit() {
    if(phase_==Phase::kCommit)return false;
    RequireTree(phase_==Phase::kAnalysis&&active_,TreeStructureError::kOwnership);
    RequireTree(revision_.value<std::numeric_limits<std::uint64_t>::max(),TreeStructureError::kResourceLimit);
    const bool transport=PassContract(*active_).preserves_demand_checkpoint&&
        demand_checkpoint_&&demand_checkpoint_->value==revision_.value;
    ++revision_.value;phase_=Phase::kCommit;
    if(transport)demand_checkpoint_=revision_;
    return true;
  }
  // Transfer only evidence of this pass's input projection, after first
  // commit. A preservation contract cannot bless stale/unversioned evidence.
  // This transports initialization alone, never flow/effect/name facts.
  [[nodiscard]] std::optional<ProjectionRevision> TransportInitializations(ProjectionRevision source) const {
    RequireTree(phase_==Phase::kCommit&&active_&&source.value==before_.value,TreeStructureError::kOwnership);
    return PassContract(*active_).preserves_initialization?std::optional{revision_}:std::nullopt;
  }
  [[nodiscard]] bool Finish() {
    RequireTree(active_.has_value(),TreeStructureError::kOwnership);
    // Only the complete ordered group can publish a demand checkpoint. A
    // caller cannot mark a dirty projection clean by hand or skip one owner.
    if(*active_==RecoveryPass::kDeadDefinitions)cleanup_=Cleanup::kInitialValues;
    else if(*active_==RecoveryPass::kInitialValues)cleanup_=Cleanup::kBindings;
    else if(*active_==RecoveryPass::kBindings){cleanup_=Cleanup::kIdle;demand_checkpoint_=revision_;}
    // A consumer checkpoint records its completed attempt, never a reusable
    // analysis result. Its own edits do not create a scheduling self-loop;
    // later projection commits make the downstream boundary reconsider it.
    if(*active_==RecoveryPass::kSnapshots)snapshot_checkpoint_=revision_;
    if(*active_==RecoveryPass::kBindingValues)binding_values_complete_=true;
    const bool changed=revision_.value!=before_.value;active_.reset();phase_=Phase::kIdle;return changed;
  }
  void EnterStorage() {
    RequireIdle();RequireTree(cleanup_==Cleanup::kIdle&&stage_==Stage::kValues&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    stage_=Stage::kStorage;
  }
  // Read-only value substitution waits for storage/snapshot destinations.
  // Once entered, aliases, storage and motion cannot rewrite those choices.
  void EnterReadValues() {
    RequireIdle();RequireTree(cleanup_==Cleanup::kIdle&&stage_==Stage::kStorage&&snapshot_checkpoint_&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    stage_=Stage::kReadValues;
  }
  void EnterGrammar() {
    RequireIdle();RequireTree(cleanup_==Cleanup::kIdle&&stage_==Stage::kReadValues&&binding_values_complete_&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    stage_=Stage::kGrammar;
  }
  [[nodiscard]] bool DemandCleanupNeeded() const {
    RequireIdle();return !demand_checkpoint_||demand_checkpoint_->value!=revision_.value;
  }
  [[nodiscard]] bool SnapshotRefreshNeeded() const {
    RequireIdle();RequireTree(stage_==Stage::kStorage&&cleanup_==Cleanup::kIdle&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    return !snapshot_checkpoint_||snapshot_checkpoint_->value!=revision_.value;
  }
 private:
  enum class Phase {kIdle,kAnalysis,kCommit};
  enum class Stage {kValues,kStorage,kReadValues,kGrammar};
  enum class Cleanup {kIdle,kInitialValues,kBindings};
  void RequireIdle() const {RequireTree(phase_==Phase::kIdle,TreeStructureError::kOwnership);}
  Phase phase_=Phase::kIdle;
  Stage stage_=Stage::kValues;
  Cleanup cleanup_=Cleanup::kIdle;
  bool binding_values_complete_=false;
  std::optional<RecoveryPass> active_;
  ProjectionRevision revision_,before_;
  std::optional<ProjectionRevision> demand_checkpoint_,snapshot_checkpoint_;
};
}
#endif
