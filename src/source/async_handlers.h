#ifndef ABCD_SOURCE_ASYNC_HANDLERS_H_
#define ABCD_SOURCE_ASYNC_HANDLERS_H_

#include <optional>
#include <span>

#include "abcd/abc/method_body.h"
#include "abcd/source/async_failure.h"

namespace abcd::source {
// The completion region/handler must be last in file order. Earlier regions
// and every referenced handler stay inside its protected body. This is not a
// license to erase user catches or to reorder overlapping dispatch regions.
bool HasAsyncCompletionCatch(const abc::MethodBody& body,
                             abc::CodeOffset begin,
                             abc::CodeOffset handler,
                             std::uint64_t code_end);

// Requires HasAsyncCompletionCatch and matching sorted original-PC provenance.
// Retains only user regions/handlers, mapping exact retained boundaries or a
// removed RESOLVE immediately preceding its retained RETURN. The removed outer
// handler entry maps to the new code end, never to a user instruction.
// removed_copies (sorted original PCs) are protocol-only promise copies; a
// boundary at one moves to the following original instruction.
std::optional<FunctionEmitError> RebaseAsyncHandlers(
    const abc::MethodBody& original, std::span<const abc::CodeOffset> origins,
    abc::MethodBody& source, std::size_t bytes, AsyncCompletionEvidence* evidence = nullptr,
    std::span<const abc::CodeOffset> removed_copies = {});
}  // namespace abcd::source
#endif
