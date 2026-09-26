#ifndef ABCD_SOURCE_CLASS_METHOD_SELECTION_H_
#define ABCD_SOURCE_CLASS_METHOD_SELECTION_H_

#include "abcd/analysis/class_method_sites.h"
#include "abcd/source/source_route.h"

namespace abcd::source {
// Explicit migration slice, not a preferred-route fallback policy. The index
// and identity belong to the same immutable input as the record invocation and
// outlive synchronous emission. Selection names an occurrence, not just a
// target method that may be reused by several class creations.
struct ClassMethodSelection {
  const analysis::ClassMethodSites& sites;
  analysis::ClassSiteId site;
  RouteInputIdentity identity;
};
}  // namespace abcd::source
#endif
