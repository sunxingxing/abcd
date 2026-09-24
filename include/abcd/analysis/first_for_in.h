#ifndef ABCD_ANALYSIS_FIRST_FOR_IN_H_
#define ABCD_ANALYSIS_FIRST_FOR_IN_H_

#include <optional>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"

namespace abcd::analysis {
struct FirstForIn {
  abc::RegisterId source;
};

// Recognizes one whole v7 frontend lowering of "return first enumerable key".
// The catch must be transparent, the source a user parameter, and every byte
// of control flow accounted for. This is not a general exception/loop lift.
[[nodiscard]] std::optional<FirstForIn> MatchFirstForIn(
    const abc::MethodBody& body, const abc::FrameLayout& frame);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_FIRST_FOR_IN_H_
