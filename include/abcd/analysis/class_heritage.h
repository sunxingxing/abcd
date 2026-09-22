#ifndef ABCD_ANALYSIS_CLASS_HERITAGE_H_
#define ABCD_ANALYSIS_CLASS_HERITAGE_H_
#include "abcd/abc/method_body.h"

namespace abcd::analysis {
enum class HeritageKind { kUnknown, kAbsent, kNull };
struct ClassHeritage {
  abc::CodeOffset pc;
  HeritageKind kind;
};
enum class HeritageError { kFrameRange, kMalformedInstruction, kResourceLimit };
struct HeritageBudget {
  std::size_t bytes = 16 * 1024 * 1024;
};
// Requires validated decoded body with branch/handler targets. Local constant
// propagation only: incoming control flow and unknown operations discard facts.
// Hole means no extends, null means DERIVED, not base (v7.0 RuntimeStubs::
// RuntimeSetClassInheritanceRelationship). No target/name/ownership proof.
// N instructions, R registers, E incoming edges, C modern ordinary class sites:
// O(N + R + E log E) time, O(R + E + C) bounded storage. Epoch invalidation
// avoids clearing R registers per block or unknown operation.
[[nodiscard]] std::variant<std::vector<ClassHeritage>, HeritageError>
ProveClassHeritage(const abc::MethodBody& body, std::uint32_t registers,
                   HeritageBudget budget = {});
}  // namespace abcd::analysis
#endif
