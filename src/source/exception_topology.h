#ifndef ABCD_SOURCE_EXCEPTION_TOPOLOGY_H_
#define ABCD_SOURCE_EXCEPTION_TOPOLOGY_H_

#include "control_skeleton.h"
#include "exception_control.h"

namespace abcd::source {
struct TryControlPort {
  analysis::WorkBlockId target;
  ControlNodeId destination;
};
// Private projection owner. Scratch/result grants cover both this topology and
// its mapping while the shared skeleton is discovered or independently checked.
struct ExceptionTopology {
  std::vector<ControlVertex> blocks;
  std::vector<ControlLink> edges;
  std::vector<ControlId> entries;
  std::vector<std::optional<analysis::TryId>> owners;
  std::vector<std::vector<TryControlPort>> ports;
  std::vector<ScopedControlEntry> scoped_entries;
  ControlLoopLayout loops;
  ExceptionControl mapping;
};
ExceptionTopology ProjectExceptionControl(const analysis::ValueProgram& graph,
    const ExceptionPlan& regions, ControlQuota& quota);
ExceptionTopology ProjectProtocolControl(const analysis::ValueProgram&, const ExceptionPlan&,
    const analysis::PhasedValueInput&, const VerifiedProtocolRegionOwnership&, ControlQuota&);
void ComposeExceptionLoops(ExceptionTopology& topology, const ExceptionPlan& regions,
    const analysis::ValueProgram& graph, ControlQuota& quota);
}  // namespace abcd::source
#endif
