#ifndef ABCD_SOURCE_GENERATOR_COMPLETION_H_
#define ABCD_SOURCE_GENERATOR_COMPLETION_H_

#include "abcd/analysis/yield_region.h"
#include "generator_delegate.h"
#include "abcd/source/async_completion.h"

namespace abcd::source {
enum class GeneratorExecution : std::uint8_t { kSynchronous, kAsynchronous };
struct GeneratorBody {
  abc::MethodBody body;
  std::vector<abc::CodeOffset> source_pcs;
  std::vector<analysis::YieldSite> yields;
  std::vector<analysis::AwaitSite> awaits;
};
// Normalize decoded OH7 synchronous generator entry and direct/delegated exits.
// Resume dispatch uses the common typed JEQZ predicate, independent of encoding
// width; destinations must still skip exactly the RETURN and THROW routes.
// Keep user handlers through shared catch-input analysis and exact PC rebasing.
// Preserve inline RETURN cleanup in the common value/control graph;
// CompletionPlan/FunctionTree route those effects at the native yield without
// reading its private pending payload. Cleanup may not observe/clobber resume
// slots or depend on the dispatch accumulator. Common normal CFG proves
// accumulator availability, including across block boundaries; SSA owns values
// and merges. Cleanup may suspend at another certified direct yield with
// independent result/mode slots. OH7 handleReturn_ prevents reinjecting outer
// cleanup; nested RETURN routes remain unsupported. Branches stay within the
// cleanup/payload boundary and cannot enter inner resume interiors. Native
// RETURN/THROW at the inner yield overrides the outer pending completion.
// Never erase user effects or admit a coroutine by schema resemblance.
// Exact synchronous YieldStar folds to ValueYield(kDelegate) at GetIterator;
// only its matched Iterator.Close handler is removed. All private slots must
// be dead at continuation/user catch, and no user boundary/interior entry may
// bisect delegation. Injected RETURN cleanup in yield* remains unsupported.
// Async execution currently admits exact Prepare + ImplicitReturn/CleanUp
// with ordinary source effects and shared AwaitRegion continuations between
// them. Entry result/mode slots may be defined by certified awaits; other
// source reuse, raw yield and explicit return remain unsupported. Native
// async generator owns its request queue and settlement, never an async writer.
// I instructions,
// B branches, H handlers, Y yields, R dispatch spans: normalization/rebasing
// O(I+(Y+B+H) log(I+1)), shared catch analysis O((I+Y+H) log(Y+1)+I log(R+1))
// after dispatch construction; O(I+Y+R) storage.
[[nodiscard]] std::variant<GeneratorBody, FunctionEmitError, abc::MetadataError> NormalizeGenerator(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    abc::ProfileId profile, ResourceFailureScope resources, std::size_t bytes, const GeneratorNames* names = nullptr,
    GeneratorExecution execution = GeneratorExecution::kSynchronous);
}  // namespace abcd::source
#endif
