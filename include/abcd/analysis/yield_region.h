#ifndef ABCD_ANALYSIS_YIELD_REGION_H_
#define ABCD_ANALYSIS_YIELD_REGION_H_

#include <optional>

#include "abcd/abc/method_body.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
// An inline RETURN-only corridor remains in the common value/control graph.
// Its preserving payload is private; cleanup must not read/write resume slots.
struct YieldReturnRoute {
  abc::CodeOffset branch, cleanup, payload;
  // Absent when every reachable cleanup exit overrides pending RETURN.
  std::optional<abc::CodeOffset> completion;
  bool operator==(const YieldReturnRoute&) const = default;
};
struct YieldDelegateRegisters {
  abc::RegisterId first;
  std::uint32_t count;
  bool operator==(const YieldDelegateRegisters&) const = default;
};
enum class YieldForm : std::uint8_t { kDirect, kDelegate };
// Original OH7 direct/delegated suspend protocol, owned by a generator.
// Delegate create names GetIterator, value its private iterator register; the
// native delegation operand is the incoming accumulator at that instruction.
// NEXT commits the resumed value; THROW and RETURN leave the native yield.
struct YieldRegion {
  abc::CodeOffset create, suspend, resume, result_store, mode, mode_store, continuation;
  abc::RegisterId state, value, result, resume_mode;
  abc::CodeOffset branch, reject;
  std::optional<YieldReturnRoute> return_route;
  YieldForm form = YieldForm::kDirect;
  std::optional<YieldDelegateRegisters> delegate_registers;
  bool operator==(const YieldRegion&) const = default;
};
struct YieldSite {
  abc::CodeOffset pc;
  std::optional<abc::CodeOffset> mode_pc;
  YieldRegion origin;
};
}  // namespace abcd::analysis
#endif
