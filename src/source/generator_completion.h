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
// User handlers/finalizer-injected RETURN routes require further completion
// analysis; never erase them merely because a suspend resembles this schema.
// I instructions and B retained branches: O(I+B log(I+1)) time, O(I) storage.
[[nodiscard]] std::variant<GeneratorBody, FunctionEmitError> NormalizeGenerator(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    abc::ProfileId profile, ResourceFailureScope resources, std::size_t bytes);
}  // namespace abcd::source
#endif
