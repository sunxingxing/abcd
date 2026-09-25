#ifndef ABCD_ANALYSIS_AWAIT_REGION_H_
#define ABCD_ANALYSIS_AWAIT_REGION_H_

#include <cstddef>
#include <array>
#include <optional>
#include <variant>
#include <vector>

#include "abcd/abc/method_body.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
// Exact instruction sequence admitted by await_region.cc's AwaitStep schema,
// including the retained continuation load. Not a byte width or opcode value.
inline constexpr std::size_t kAwaitProtocolInstructions = 14;
struct AwaitRegion {
  abc::CodeOffset await;
  abc::CodeOffset suspend;
  abc::CodeOffset resume;
  abc::CodeOffset result_store;
  abc::CodeOffset mode;
  abc::CodeOffset mode_store;
  abc::CodeOffset branch;
  abc::CodeOffset reject;
  abc::CodeOffset continuation;
  abc::RegisterId state;
  abc::RegisterId result;
  abc::RegisterId resume_mode;
  std::optional<std::array<abc::CodeOffset, kAwaitProtocolInstructions>> instructions;
};
// A callable owner supplies this only after proving its async completion
// wrapper and private state, then maps await and mode PCs to the normalized
// body. Normal native-await continuation has exact NEXT mode; rejection exits
// before that mode definition. Neither site consumes the private VM state.
struct AwaitSite {
  abc::CodeOffset pc;
  AwaitRegion origin;
  abc::CodeOffset mode_pc;
};
enum class AwaitRegionError {
  kUnsupportedProfile,
  kUnsupportedShape,
  kInteriorEntry,
  kExceptionBoundary,
  kResourceLimit
};
struct AwaitRegionFailure {
  AwaitRegionError kind;
  abc::CodeOffset pc;
};
using AwaitRegions = std::variant<std::vector<AwaitRegion>, AwaitRegionFailure>;

// Requires DecodeMethodBody output. Establishes local await/resume pairing,
// rejection rethrow and single entry, not ownership of the private async
// state, catch wrapper or temporary-register liveness. Those must be proved
// before replacing any instruction. Retain all PCs for eventual source spans.
// O(I + (B + T + H) log(A + 1)), O(A) storage within bytes.
[[nodiscard]] AwaitRegions FindAwaitRegions(const abc::MethodBody& body,
                                            abc::ProfileId profile,
                                            std::uint64_t frame_size,
                                            std::size_t bytes);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_AWAIT_REGION_H_
