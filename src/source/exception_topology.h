#ifndef ABCD_SOURCE_EXCEPTION_TOPOLOGY_H_
#define ABCD_SOURCE_EXCEPTION_TOPOLOGY_H_

#include "control_skeleton.h"
#include "exception_control.h"

namespace abcd::source {
// Private projection owner. Scratch/result grants cover both this topology and
// its mapping while the shared skeleton is discovered or independently checked.
struct ExceptionTopology {
  std::vector<ControlVertex> blocks;
  std::vector<ControlLink> edges;
  std::vector<ControlId> entries;
  ExceptionControl mapping;
};
ExceptionTopology ProjectExceptionControl(const analysis::ValueProgram& graph,
    const ExceptionPlan& regions, ControlQuota& quota);
}  // namespace abcd::source
#endif
