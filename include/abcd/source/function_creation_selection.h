#ifndef ABCD_SOURCE_FUNCTION_CREATION_SELECTION_H_
#define ABCD_SOURCE_FUNCTION_CREATION_SELECTION_H_

#include "abcd/analysis/function_creation_sites.h"
#include "abcd/source/source_route.h"

namespace abcd::source {
// Same immutable file/index as the enclosing synchronous record invocation.
// Select an occurrence, not every reuse of its target method. No legacy retry
// is permitted at this creation, even if the selected family is unsupported.
struct FunctionCreationSelection {
  const analysis::FunctionCreationSites& sites;
  analysis::FunctionSiteId site;
  RouteInputIdentity identity;
};
}  // namespace abcd::source
#endif
