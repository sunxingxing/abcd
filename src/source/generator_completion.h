#ifndef ABCD_SOURCE_GENERATOR_COMPLETION_H_
#define ABCD_SOURCE_GENERATOR_COMPLETION_H_

#include "abcd/analysis/yield_region.h"
#include "abcd/source/async_completion.h"

namespace abcd::source {
struct GeneratorBody {
  abc::MethodBody body;
  std::vector<abc::CodeOffset> source_pcs;
  std::vector<analysis::YieldSite> yields;
};
// Normalize decoded OH7 synchronous generator entry and direct resume exits.
// Keep user handlers through shared catch-input analysis and exact PC rebasing.
// Finalizer-injected RETURN routes require further completion analysis; never
// erase them merely because a suspend resembles this schema. I instructions,
// B branches, H handlers, Y yields, R dispatch spans: normalization/rebasing
// O(I+(B+H) log(I+1)), shared catch analysis O((I+Y+H) log(Y+1)+I log(R+1))
// after dispatch construction; O(I+Y+R) storage.
[[nodiscard]] std::variant<GeneratorBody, FunctionEmitError> NormalizeGenerator(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    abc::ProfileId profile, ResourceFailureScope resources, std::size_t bytes);
}  // namespace abcd::source
#endif
