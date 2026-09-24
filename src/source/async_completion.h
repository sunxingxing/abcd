#ifndef ABCD_SOURCE_ASYNC_COMPLETION_H_
#define ABCD_SOURCE_ASYNC_COMPLETION_H_

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/source/base_function.h"

namespace abcd::source {
// The v7 compiler's no-await completion template can be represented by a
// native async method: its catch/reject tail is exactly the async call's
// implicit rejection path. Other suspension and handler shapes are refused.
// Optional provenance maps each retained normalized instruction to its ABC PC;
// the caller owns it and its capacity shares the normalization byte budget.
using AsyncCompletion = std::variant<abc::MethodBody, FunctionEmitError>;
[[nodiscard]] AsyncCompletion ProveAsyncCompletion(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::size_t bytes, std::vector<abc::CodeOffset>* source_pcs = nullptr);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ASYNC_COMPLETION_H_
