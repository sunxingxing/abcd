#ifndef ABCD_ANALYSIS_EXCEPTION_THROW_DOMAIN_H_
#define ABCD_ANALYSIS_EXCEPTION_THROW_DOMAIN_H_

#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/raw_event_graph.h"

namespace abcd::analysis {
// A constraint on ONE explicit throw's dispatch payload, not a global upgrade
// of its SSA value. Copies retain identity; phis remain distinct definitions.
struct ExceptionThrowDomain {
  ExceptionNodeId site;
  SsaValueId payload;
  SsaValueId compared;
  ExceptionNodeId comparison;
  ExceptionNodeId branch;
  std::uint8_t successor = 0;
  auto operator<=>(const ExceptionThrowDomain&) const = default;
};
// Equality's unequal edge excludes Hole from this same dynamic value. The
// edge must dominate the throw in the full exception CFG; a cyclic definition
// is not refined by a potentially stale comparison from a previous activation.
// O((N+E+V+U) log(N+V)), PMR scratch charged by arena before allocation.
[[nodiscard]] std::pmr::vector<ExceptionThrowDomain> DeriveExceptionThrowDomains(
    const ExceptionFlow&, const ExceptionSsa&, RawEventArena&);
void VerifyExceptionThrowDomains(const ExceptionFlow&, const ExceptionSsa&,
    std::span<const ExceptionThrowDomain>, RawEventArena&);
}  // namespace abcd::analysis
#endif
