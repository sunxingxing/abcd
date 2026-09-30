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
struct YieldPrivateRegisters {
  abc::RegisterId first;
  std::uint32_t count;
  bool operator==(const YieldPrivateRegisters&) const = default;
};
enum class YieldForm : std::uint8_t { kDirect, kDelegate };
enum class YieldExecution : std::uint8_t { kSynchronous, kAsynchronous };
struct YieldAwaitRejections {
  abc::CodeOffset value, returned;
  bool operator==(const YieldAwaitRejections&) const = default;
};
// Original OH7 direct/delegated synchronous or direct async yield protocol.
// Delegate create names GetIterator, value its private iterator register; the
// native delegation operand is the incoming accumulator at that instruction.
// NEXT commits the resumed value; THROW and RETURN leave the native yield.
// Async direct create names its outbound await anchor and reads the incoming
// accumulator. Both implicit waits belong to native yield, not AwaitExpr.
struct YieldRegion {
  abc::CodeOffset create, suspend, resume, result_store, mode, mode_store, continuation;
  abc::RegisterId state, value, result, resume_mode;
  abc::CodeOffset branch, reject;
  std::optional<YieldReturnRoute> return_route;
  YieldForm form = YieldForm::kDirect;
  std::optional<YieldPrivateRegisters> private_registers;
  YieldExecution execution = YieldExecution::kSynchronous;
  // Native async yield owns both implicit waits, in addition to THROW.
  std::optional<YieldAwaitRejections> awaited_rejections;
  bool operator==(const YieldRegion&) const = default;
};
struct YieldSite {
  abc::CodeOffset pc;
  std::optional<abc::CodeOffset> mode_pc;
  YieldRegion origin;
};
}  // namespace abcd::analysis
#endif
