#ifndef ABCD_SOURCE_ASYNC_COMPLETION_H_
#define ABCD_SOURCE_ASYNC_COMPLETION_H_

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/source/function_emit_error.h"
#include "abcd/analysis/await_region.h"
#include "abcd/source/source_contract.h"

namespace abcd::source {
// The v7 compiler's completion template can be represented by a
// native async method: its catch/reject tail is exactly the async call's
// implicit rejection path. With awaits supplied, verified modern resume
// regions are collapsed while retaining await, result and mode definitions;
// only the generic IR may interpret these using the returned AwaitSite facts.
// The exact final completion catch is removed; contained user handlers remain
// in file order with rebased ranges. Other suspension/wrapper shapes are refused.
// Requires decoded input. Exception-source admission is a separate proof.
// The origin index rebases branches, including targets at removed settlement
// operations. Optional provenance publishes it to the caller. All retained
// arrays and temporary indexing share the normalization byte budget.
// A leading lexical allocation is retained only under an explicit exclusion
// of VM resource-exhaustion differences. Named-scope metadata and all lexical
// uses still require the shared source owner's independent validation.
using AsyncCompletion = std::variant<abc::MethodBody, FunctionEmitError>;
[[nodiscard]] AsyncCompletion ProveAsyncCompletion(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::size_t bytes, std::vector<abc::CodeOffset>* source_pcs = nullptr,
    std::vector<analysis::AwaitSite>* awaits = nullptr,
    abc::ProfileId profile = abc::ProfileId::kUnknown,
    ResourceFailureScope resources = ResourceFailureScope::kObservable);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_ASYNC_COMPLETION_H_
