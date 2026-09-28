#ifndef ABCD_SOURCE_CONTROL_SKELETON_H_
#define ABCD_SOURCE_CONTROL_SKELETON_H_

#include "control_plan.h"

namespace abcd::source {
struct ScopedControlEntry {
  ControlNodeId node;
  std::optional<LoopId> scope;
};
// Lexical composition of already discovered natural loops. A contained region
// can enter inside an enclosing loop without being a new dominance root there.
struct ControlLoopLayout {
  std::vector<NaturalLoop> loops;
  std::vector<std::optional<LoopId>> owners;
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
// Extend lexical ownership through single-entry exit arms, inner
// loops first. Natural-loop discovery stays unchanged; joins and roots stop it.
void ExtendLoopExitCorridors(ControlGraphView graph, std::span<const ControlNodeId> entries,
    ControlLoopLayout& loops, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildControlSkeleton(const analysis::ValueProgram& graph,
    std::span<const ControlId> entries, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildControlSkeleton(ControlGraphView graph,
    std::span<const ControlId> entries, ControlQuota& quota);
// Independent edge/ownership proof for an acyclic forest. Does not rediscover
// regions or postdominators, and does not certify the caller's projection.
void VerifyControlSkeleton(ControlGraphView graph, std::span<const ControlId> entries,
    const ControlPlan& plan, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildLoopControl(const analysis::ValueProgram& graph, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildLoopControl(ControlGraphView graph,
    std::span<const ControlNodeId> entries, ControlQuota& quota);
[[nodiscard]] ControlPlan BuildLoopControl(ControlGraphView graph,
    std::span<const ScopedControlEntry> entries, ControlLoopLayout loops, ControlQuota& quota);
void VerifyControlRegions(ControlGraphView graph, std::span<const ControlNodeId> entries,
    const ControlPlan& plan, ControlQuota& quota);
void VerifyControlRegions(ControlGraphView graph, std::span<const ScopedControlEntry> entries,
    const ControlPlan& plan, ControlQuota& quota);
}  // namespace abcd::source
#endif
