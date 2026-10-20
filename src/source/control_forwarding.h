#ifndef ABCD_SOURCE_CONTROL_FORWARDING_H_
#define ABCD_SOURCE_CONTROL_FORWARDING_H_

#include "control_skeleton.h"

namespace abcd::source {
// Epsilon control ports split ownership of an unobservable shared corridor.
// Original operations, phi inputs and original edge IDs remain single-owned.
[[nodiscard]] ControlPlan BuildForwardingControl(const analysis::ValueProgram& graph,
    const ControlFailure& failure, ControlQuota& quota);
void VerifyForwardingControl(const analysis::ValueProgram& graph,
    const ControlPlan& plan, ControlQuota& quota);
}  // namespace abcd::source
#endif
