#ifndef ABCD_ANALYSIS_CATCH_PAYLOAD_DOMAIN_H_
#define ABCD_ANALYSIS_CATCH_PAYLOAD_DOMAIN_H_
#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/raw_event_graph.h"
namespace abcd::analysis {
enum class ExternalExceptionPolicy : std::uint8_t { kPinnedRuntime, kNoHoleV1 };
enum class CatchPayloadDomain : std::uint8_t { kHolePossible, kNonHole };
struct CatchPayloadFact {
  ExceptionNodeId entry;
  CatchPayloadDomain domain;
  std::uint32_t sources;
  auto operator<=>(const CatchPayloadFact&) const = default;
};
// Immutable original dispatch/SSA only. All incoming sources participate;
// normal jumps into handler instructions do not define a Catch payload.
// Constants/copies/phis/local throws use a least fixed point: an unsupported or
// cyclic definition is unknown, never a language-value assumption. PMR buffers
// are prepaid by the caller's arena and may not outlive it. O((V+E) log V).
[[nodiscard]] std::pmr::vector<CatchPayloadFact> DeriveCatchPayloadDomains(
    const ExceptionFlow&, const ExceptionSsa&, ExternalExceptionPolicy, RawEventArena&);
// Rebuilds all source equations from immutable inputs, never supplied flags.
void VerifyCatchPayloadDomains(const ExceptionFlow&, const ExceptionSsa&,
    ExternalExceptionPolicy, std::span<const CatchPayloadFact>, RawEventArena&);
}  // namespace abcd::analysis
#endif
