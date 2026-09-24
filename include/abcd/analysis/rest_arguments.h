#ifndef ABCD_ANALYSIS_REST_ARGUMENTS_H_
#define ABCD_ANALYSIS_REST_ARGUMENTS_H_

#include <cstdint>
#include <optional>
#include <variant>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"

namespace abcd::analysis {
struct RestCopy {
  std::uint16_t first_argument;
};
enum class RestError { kUnsupportedOpcode, kMalformedInstruction };
struct RestPrologue {
  std::uint16_t first_argument;
  abc::RegisterId local;
};

[[nodiscard]] std::variant<RestCopy, RestError> LiftRestArguments(
    const abc::DecodedInstruction& instruction);

// The pinned v7 frontend first copies every declared ABI slot to locals,
// then replaces the copied rest slot with CopyRestArgs. Only this exact
// pattern permits source `...rest`; all later reads of the original ABI slot
// must be absent. The proof is linear in instruction count and uses no heap.
[[nodiscard]] std::optional<RestPrologue> ProveRestPrologue(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::uint16_t source_length);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REST_ARGUMENTS_H_
