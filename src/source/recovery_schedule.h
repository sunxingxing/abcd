#ifndef ABCD_SOURCE_RECOVERY_SCHEDULE_H_
#define ABCD_SOURCE_RECOVERY_SCHEDULE_H_
#include "tree_checks.h"
#include <initializer_list>
#include <array>
#include <limits>

namespace abcd::source {
enum class RecoveryPass {
  kDeclarations,kConstants,kBindingValues,kAliases,kStoredResults,kDeadDefinitions,kInitialValues,
  kBindings,kSnapshots,kStorage,kStatementValues,kInertStatements,kInertOperands,kLoopDeclarations,kDeclarationKinds
};
enum class RecoveryFact : std::uint8_t {kExpressions,kFlow,kLiveness,kDemand,kInitializations,kConstants,kCopies,kStatements,kCycles,kLoopEntries};
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
enum class RecoveryPrerequisite {kNone,kValueCleanup,kReadValueCleanup};
enum class RecoveryBarrier {kDemand,kStorage,kSnapshotRefresh,kReadValues,kGrammar};
struct RecoveryPassContract {
  RecoveryFactSet inputs;
  RecoveryPassScope scope;
  bool preserves_demand_checkpoint=false;
  RecoveryPrerequisite prerequisite=RecoveryPrerequisite::kNone;
};
// These contracts describe semantic dependencies, never bytecode/source
// admission. Providers close dependencies lazily; no eager graph rebuild is
// introduced. Occurrence initialization is rebuilt on every projection.
[[nodiscard]] inline RecoveryPassContract PassContract(RecoveryPass pass) {
  using Fact=RecoveryFact;using Scope=RecoveryPassScope;using Prerequisite=RecoveryPrerequisite;
  switch(pass) {
    case RecoveryPass::kDeclarations:return {{Fact::kFlow},Scope::kValues,false,Prerequisite::kValueCleanup};
    case RecoveryPass::kConstants:return {{Fact::kConstants},Scope::kValues};
    case RecoveryPass::kBindingValues:return {{Fact::kConstants,Fact::kCopies},Scope::kReadValues};
    case RecoveryPass::kAliases:return {{Fact::kLiveness},Scope::kValueOrStorage,false,Prerequisite::kValueCleanup};
    // Store identity elision retains RHS completion at its occurrence.
    case RecoveryPass::kStoredResults:return {{Fact::kExpressions},Scope::kNormalization};
    // Removed definitions/initial values/bindings leave no retained reference
    // to the removed completion; other occurrence initialization is unchanged.
    case RecoveryPass::kDeadDefinitions:case RecoveryPass::kInitialValues:case RecoveryPass::kBindings:
      return {{Fact::kDemand},Scope::kNormalization};
    // A snapshot removes its own single definition/read while preserving the
    // RHS at the destination. It cannot make another producer newly dead.
    // Transport only an already established checkpoint, never create one.
    case RecoveryPass::kSnapshots:return {{Fact::kLiveness},Scope::kStorage,true};
    // Coalescing consumes current slot initialization; the next projection
    // independently re-establishes each occurrence's completed initialization.
    case RecoveryPass::kStorage:return {{Fact::kLiveness,Fact::kInitializations},Scope::kStorage};
    // Preserve mandatory evaluation at its exact occurrence; only the common
    // inert residual disappears. Final read destinations and cleanup precede it.
    case RecoveryPass::kStatementValues:return {{Fact::kStatements},Scope::kReadValues,false,Prerequisite::kReadValueCleanup};
    // Only proven inert occurrences disappear; no retained initialization moves.
    case RecoveryPass::kInertStatements:return {{Fact::kStatements},Scope::kNormalization};
    case RecoveryPass::kInertOperands:
      return {{Fact::kExpressions},Scope::kNormalization};
    case RecoveryPass::kLoopDeclarations:return {{Fact::kLoopEntries},Scope::kGrammar};
    // Grammar-only const spelling changes neither values nor completion.
    case RecoveryPass::kDeclarationKinds:return {{Fact::kFlow},Scope::kGrammar};
  }
  throw TreeStructureError::kOwnership;
}
[[nodiscard]] inline RecoveryFactSet RequiredFacts(RecoveryPass pass) {
  auto result=PassContract(pass).inputs;
  if(result.Contains(RecoveryFact::kStatements))result.Add(RecoveryFact::kCycles);
  if(result.Contains(RecoveryFact::kConstants)||result.Contains(RecoveryFact::kCopies))result.Add(RecoveryFact::kLiveness);
  if(result.Contains(RecoveryFact::kCycles)||result.Contains(RecoveryFact::kLiveness)||result.Contains(RecoveryFact::kDemand)||
      result.Contains(RecoveryFact::kInitializations))result.Add(RecoveryFact::kFlow);
  // Flow and expressions are bootstrapped together: conservative effects ->
  // initialization proof -> refined effects. They are one provider boundary,
  // not mutually scheduled optimization passes.
  if(result.Contains(RecoveryFact::kFlow)||result.Contains(RecoveryFact::kExpressions)) {
    result.Add(RecoveryFact::kFlow);result.Add(RecoveryFact::kExpressions);result.Add(RecoveryFact::kLoopEntries);
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
    else RequireTree(stage_==Stage::kGrammar&&(fact==RecoveryFact::kExpressions||fact==RecoveryFact::kFlow||fact==RecoveryFact::kLoopEntries),TreeStructureError::kOwnership);
  }
  // Return one dependency owner, never execute or mutate a plan here. The
  // runner completes it against current facts before asking again. Cleanup
  // advances through its three independent owners; read values precede cleanup.
  // This finite prerequisite graph is not a whole-function fixed-point loop.
  [[nodiscard]] std::optional<RecoveryPass> Prerequisite(RecoveryPass pass) const {
    RequireIdle();RequireScope(pass);
    if(const auto pending=PendingCleanup();pending&&*pending!=pass)return pending;
    const auto requirement=PassContract(pass).prerequisite;
    if(requirement==RecoveryPrerequisite::kReadValueCleanup&&!binding_values_complete_)return RecoveryPass::kBindingValues;
    if((requirement==RecoveryPrerequisite::kReadValueCleanup||
        (requirement==RecoveryPrerequisite::kValueCleanup&&stage_==Stage::kValues))&&DemandCleanupNeeded())
      return RecoveryPass::kDeadDefinitions;
    return {};
  }
  [[nodiscard]] std::optional<RecoveryPass> Prerequisite(RecoveryBarrier barrier) const {
    RequireIdle();RequireBarrier(barrier);
    if(const auto pending=PendingCleanup())return pending;
    if(barrier==RecoveryBarrier::kReadValues&&!snapshot_checkpoint_)return RecoveryPass::kSnapshots;
    if(barrier==RecoveryBarrier::kGrammar&&!binding_values_complete_)return RecoveryPass::kBindingValues;
    if(DemandCleanupNeeded())return RecoveryPass::kDeadDefinitions;
    return {};
  }
  void Arrive(RecoveryBarrier barrier) {
    RequireTree(!Prerequisite(barrier),TreeStructureError::kOwnership);
    switch(barrier) {
      case RecoveryBarrier::kDemand:break;
      case RecoveryBarrier::kStorage:EnterStorage();break;
      case RecoveryBarrier::kSnapshotRefresh:(void)SnapshotRefreshNeeded();break;
      case RecoveryBarrier::kReadValues:EnterReadValues();break;
      case RecoveryBarrier::kGrammar:EnterGrammar();break;
    }
  }
  void Start(RecoveryPass pass) {
    RequireIdle();RequireScope(pass);
    RequireTree(cleanup_==Cleanup::kIdle||
        (cleanup_==Cleanup::kInitialValues&&pass==RecoveryPass::kInitialValues)||
        (cleanup_==Cleanup::kBindings&&pass==RecoveryPass::kBindings),TreeStructureError::kOwnership);
    if(pass==RecoveryPass::kInitialValues)RequireTree(cleanup_==Cleanup::kInitialValues,TreeStructureError::kOwnership);
    if(pass==RecoveryPass::kBindings)RequireTree(cleanup_==Cleanup::kBindings,TreeStructureError::kOwnership);
    const auto requirement=PassContract(pass).prerequisite;
    if(requirement==RecoveryPrerequisite::kValueCleanup&&stage_==Stage::kValues)
      RequireTree(!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    if(requirement==RecoveryPrerequisite::kReadValueCleanup)
      RequireTree(binding_values_complete_&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    active_=pass;before_=revision_;phase_=Phase::kAnalysis;
  }
  // A stable attempt is specific to this unchanged projection and phase.
  // The runner still executes Start/Finish so dependency checkpoints advance.
  [[nodiscard]] bool AlreadyStable() const {
    RequireTree(phase_==Phase::kAnalysis&&active_,TreeStructureError::kOwnership);
    const auto& stable=stable_[static_cast<std::size_t>(*active_)];
    return stable&&stable->value==revision_.value;
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
    const bool changed=revision_.value!=before_.value;
    if(!changed)stable_[static_cast<std::size_t>(*active_)]=revision_;
    active_.reset();phase_=Phase::kIdle;return changed;
  }
  void EnterStorage() {
    RequireIdle();RequireTree(cleanup_==Cleanup::kIdle&&stage_==Stage::kValues&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    stable_={};stage_=Stage::kStorage;
  }
  // Read-only value substitution waits for storage/snapshot destinations.
  // Once entered, aliases, storage and motion cannot rewrite those choices.
  void EnterReadValues() {
    RequireIdle();RequireTree(cleanup_==Cleanup::kIdle&&stage_==Stage::kStorage&&snapshot_checkpoint_&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    stable_={};stage_=Stage::kReadValues;
  }
  void EnterGrammar() {
    RequireIdle();RequireTree(cleanup_==Cleanup::kIdle&&stage_==Stage::kReadValues&&binding_values_complete_&&!DemandCleanupNeeded(),TreeStructureError::kOwnership);
    stable_={};stage_=Stage::kGrammar;
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
  [[nodiscard]] std::optional<RecoveryPass> PendingCleanup() const {
    switch(cleanup_) {
      case Cleanup::kIdle:return {};
      case Cleanup::kInitialValues:return RecoveryPass::kInitialValues;
      case Cleanup::kBindings:return RecoveryPass::kBindings;
    }
    throw TreeStructureError::kOwnership;
  }
  void RequireScope(RecoveryPass pass) const {
    const auto scope=PassContract(pass).scope;
    const bool allowed=scope==RecoveryPassScope::kValues?stage_==Stage::kValues:
        scope==RecoveryPassScope::kStorage?stage_==Stage::kStorage:
        scope==RecoveryPassScope::kReadValues?stage_==Stage::kReadValues:
        scope==RecoveryPassScope::kGrammar?stage_==Stage::kGrammar:
        scope==RecoveryPassScope::kNormalization?stage_!=Stage::kGrammar:
        stage_==Stage::kValues||stage_==Stage::kStorage;
    RequireTree(allowed,TreeStructureError::kOwnership);
  }
  void RequireBarrier(RecoveryBarrier barrier) const {
    switch(barrier) {
      case RecoveryBarrier::kDemand:RequireTree(stage_!=Stage::kGrammar,TreeStructureError::kOwnership);break;
      case RecoveryBarrier::kStorage:RequireTree(stage_==Stage::kValues,TreeStructureError::kOwnership);break;
      case RecoveryBarrier::kSnapshotRefresh:case RecoveryBarrier::kReadValues:
        RequireTree(stage_==Stage::kStorage,TreeStructureError::kOwnership);break;
      case RecoveryBarrier::kGrammar:RequireTree(stage_==Stage::kReadValues,TreeStructureError::kOwnership);break;
    }
  }
  void RequireIdle() const {RequireTree(phase_==Phase::kIdle,TreeStructureError::kOwnership);}
  // One optional revision per typed pass, independent of function size.
  std::array<std::optional<ProjectionRevision>,
      static_cast<std::size_t>(RecoveryPass::kDeclarationKinds)+1> stable_{};
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
