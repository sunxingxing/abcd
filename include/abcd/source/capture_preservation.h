#ifndef ABCD_SOURCE_CAPTURE_PRESERVATION_H_
#define ABCD_SOURCE_CAPTURE_PRESERVATION_H_

#include "abcd/analysis/capture_effect.h"
#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct CapturePreservations {
  std::vector<analysis::CapturePreservation> sites;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return sites.capacity() * sizeof(analysis::CapturePreservation);
  }
};
// Inspect reachable child templates once per target, then propagate unknown
// effects through reverse dependencies. No source text or sidecar evidence.
// Unsupported children yield no certificate; resource exhaustion is explicit.
std::variant<CapturePreservations, SourceFailure> ProveCapturePreservation(
    const abc::MethodBody& body, const MethodConstants& constants,
    const abc::MethodLookup* methods, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CAPTURE_PRESERVATION_H_
