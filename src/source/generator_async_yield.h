#ifndef ABCD_SOURCE_GENERATOR_ASYNC_YIELD_H_
#define ABCD_SOURCE_GENERATOR_ASYNC_YIELD_H_
#include "abcd/abc/frame_layout.h"
#include "abcd/analysis/await_region.h"
#include "abcd/analysis/yield_region.h"
#include "abcd/source/function_emit_error.h"

namespace abcd::source {
// Exact OH7 AsyncGeneratorFunctionBuilder::Yield, without injected finally.
// Fold both intrinsic awaits into one native yield; emitting a source await
// here changes scheduling. Ordinary source awaits remain separate certificates.
struct AsyncGeneratorYield {
  analysis::YieldRegion origin;
  std::size_t begin, end;
};
[[nodiscard]] std::variant<AsyncGeneratorYield, FunctionEmitError> ParseAsyncGeneratorYield(
    const abc::MethodBody& body, std::size_t begin, abc::RegisterId state,
    const abc::FrameLayout& frame, const analysis::AwaitRegion& value,
    const analysis::AwaitRegion& returned);
// Instruction schema positions, distinct from byte widths. Used only to find
// the second shared AwaitRegion and validate this exact compiler expansion.
inline constexpr std::size_t kAsyncYieldReturnAwait = 30;
}
#endif
