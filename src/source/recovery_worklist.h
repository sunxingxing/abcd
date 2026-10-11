#ifndef ABCD_SOURCE_RECOVERY_WORKLIST_H_
#define ABCD_SOURCE_RECOVERY_WORKLIST_H_
#include "recovery_schedule.h"
namespace abcd::source {
// The queue owns dependency scheduling only. RecoverySchedule owns ordered
// cleanup and stage barriers; algorithms own semantic eligibility and progress.
class RecoveryWorklist {
 public:
  explicit RecoveryWorklist(RecoveryStage stage):stage_(stage) {
    for(std::size_t i=0;i<order_.size();++i)pending_[i]=RecoveryPassAllowed(order_[i],stage_);
  }
  void Changed(RecoveryDomains domains) {
    for(std::size_t i=0;i<order_.size();++i)
      if(RecoveryPassAllowed(order_[i],stage_)&&ReadDomains(order_[i]).Intersects(domains))pending_[i]=true;
  }
  [[nodiscard]] std::optional<RecoveryPass> Next() {
    for(std::size_t count=0;count<order_.size();++count) {
      const auto i=next_;next_=(next_+1)%order_.size();
      if(pending_[i]){pending_[i]=false;return order_[i];}
    }
    return {};
  }
 private:
  // InitialValues and Bindings are indivisible ordered prerequisites of the
  // demand group, not independently queued opportunities. Priority affects
  // scheduling only; every changed input reschedules its consumers, including
  // the producer itself. Resume after the last consumer instead of restarting
  // at the first pass after each edit; pending consumers cannot be starved by
  // newly enabled earlier work. The queue has constant capacity independent of N.
  static constexpr std::array order_{
      RecoveryPass::kDeadDefinitions,RecoveryPass::kInertOperands,RecoveryPass::kInertStatements,
      RecoveryPass::kStoredResults,RecoveryPass::kConstants,RecoveryPass::kBindingValues,RecoveryPass::kDeclarations,
      RecoveryPass::kAliases,RecoveryPass::kSnapshots,RecoveryPass::kStorage,
      RecoveryPass::kStatementValues};
  std::array<bool,order_.size()> pending_{};
  std::size_t next_=0;
  RecoveryStage stage_;
};
}
#endif
