#ifndef ABCD_SOURCE_FAMILY_ROUTE_FAILURE_H_
#define ABCD_SOURCE_FAMILY_ROUTE_FAILURE_H_

#include "family_emission.h"

namespace abcd::source {
// Preserve resource/work/output classes and available original PCs. Remaining
// unclosed semantic premises map to Unproved, never successful source.
RouteFailure FamilyRouteFailure(const FamilyPrepareFailure& failure);
RouteFailure FamilyRouteFailure(const FamilyEmitFailure& failure);
RouteFailure FamilyRouteFailure(const analysis::FunctionSiteFailure& failure);
}  // namespace abcd::source
#endif
