#ifndef ABCD_SOURCE_PARAMETER_ALIASES_H_
#define ABCD_SOURCE_PARAMETER_ALIASES_H_

#include <cstdint>
#include <span>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"

namespace abcd::source {
// Caller owns P+1 admitted scratch counters. On success, [0,P) contains
// may-write counts; zero permits aliasing an independently validated formal.
// Unknown access leaves no usable certificate. O(instructions + parameters),
// no expansion of overlapping register windows and no per-parameter scan.
bool FindParameterWrites(const abc::MethodBody& body, const abc::FrameLayout& frame,
                         std::span<std::int64_t> counts);
}  // namespace abcd::source
#endif
