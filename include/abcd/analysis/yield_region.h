#ifndef ABCD_ANALYSIS_YIELD_REGION_H_
#define ABCD_ANALYSIS_YIELD_REGION_H_

#include "abcd/abc/method_body.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
// Original OH7 iterator-result/suspend/resume protocol, owned by a generator.
// NEXT commits the resumed value; THROW and RETURN leave the native yield.
struct YieldRegion {
  abc::CodeOffset create, suspend, resume, result_store, mode, mode_store, continuation;
  abc::RegisterId state, value, result, resume_mode;
  bool operator==(const YieldRegion&) const = default;
};
struct YieldSite {
  abc::CodeOffset pc, mode_pc;
  YieldRegion origin;
};
}  // namespace abcd::analysis
#endif
