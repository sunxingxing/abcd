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
// Resume dispatch uses the common typed JEQZ predicate, independent of encoding
// width; destinations must still skip exactly the RETURN and THROW routes.
// Keep user handlers through shared catch-input analysis and exact PC rebasing.
// Preserve inline RETURN cleanup in the common value/control graph;
// CompletionPlan/FunctionTree route those effects at the native yield without
// reading its private pending payload. Cleanup may not observe/clobber resume
// slots or depend on the dispatch accumulator. Each basic block must define
// its own accumulator input; branch targets stay inside the cleanup/payload
// boundary. Common CFG/SSA owns branches and register merges. Suspending or
// overriding cleanup needs further completion analysis; never erase it by
// schema resemblance.
// I instructions,
// B branches, H handlers, Y yields, R dispatch spans: normalization/rebasing
// O(I+(Y+B+H) log(I+1)), shared catch analysis O((I+Y+H) log(Y+1)+I log(R+1))
// after dispatch construction; O(I+Y+R) storage.
[[nodiscard]] std::variant<GeneratorBody, FunctionEmitError> NormalizeGenerator(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    abc::ProfileId profile, ResourceFailureScope resources, std::size_t bytes);
}  // namespace abcd::source
#endif
