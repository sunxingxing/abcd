#ifndef ABCD_SOURCE_CONTROL_SKELETON_H_
#define ABCD_SOURCE_CONTROL_SKELETON_H_

#include "control_plan.h"

namespace abcd::source {
// Borrowed topology only, never a ValueProgram with erased exception phases.
// Projection owners retain original block/edge identities and must validate
// their mapping before this normal-region discovery can authorize anything.
struct ControlTopology {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::span<const analysis::WorkBlock> blocks;
  std::span<const analysis::WorkEdge> edges;
};
// Shared invocation budget, including failed acyclic attempts. A skeleton is a
// candidate; only final verification against the original CFG certifies it.
class ControlQuota {
 public:
  explicit ControlQuota(analysis::SemanticBudget budget) : remaining_(budget) {}
  void Work(std::uint64_t count = 1) {
    if (count > remaining_.work) throw ControlError::kWorkLimit;
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) throw ControlError::kResourceLimit;
    remaining_.bytes -= count * width;
  }
  template <class T> void Size(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Work(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  std::uint64_t RemainingWork() const { return remaining_.work; }
  analysis::SemanticBudget Remaining() const { return remaining_; }
 private:
  analysis::SemanticBudget remaining_;
};
[[nodiscard]] ControlPlan BuildControlSkeleton(const analysis::ValueProgram& graph,
    std::span<const ControlId> entries, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildControlSkeleton(ControlTopology graph,
    std::span<const ControlId> entries, ControlQuota& quota);
// Independent edge/ownership proof for an acyclic forest. Does not rediscover
// regions or postdominators, and does not certify the caller's projection.
void VerifyControlSkeleton(ControlTopology graph, std::span<const ControlId> entries,
    const ControlPlan& plan, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildLoopControl(const analysis::ValueProgram& graph, ControlQuota& quota);
}  // namespace abcd::source
#endif
