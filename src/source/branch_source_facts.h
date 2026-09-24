#ifndef ABCD_SOURCE_BRANCH_SOURCE_FACTS_H_
#define ABCD_SOURCE_BRANCH_SOURCE_FACTS_H_

#include <cstdint>
#include <variant>
#include <vector>

#include "abcd/analysis/creation_binding.h"
#include "abcd/analysis/register_value_flow.h"
#include "abcd/source/acyclic_body.h"
#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
struct CapturedLoadSite {
  std::uint32_t instruction;
  analysis::CapturedSlot cell;
};
struct BranchSourceFacts {
  std::vector<std::uint8_t> language_loads;
  std::vector<analysis::LanguageLoadFact> value_facts;
  std::vector<CapturedLoadSite> captures;
  std::size_t AllocatedBytes() const {
    return language_loads.capacity() +
           value_facts.capacity() * sizeof(analysis::LanguageLoadFact) +
           captures.capacity() * sizeof(CapturedLoadSite);
  }
};

// Only captured reads whose language-value premise is discharged by the
// parent, and independently validated module reads, enter the ordered path.
// Local allocations/stores and ambiguous environment joins remain rejected.
[[nodiscard]] std::variant<BranchSourceFacts, BodyEmitFailure>
BuildCapturedBranchFacts(const abc::MethodBody& body,
                         std::uint64_t frame_size,
                         const MethodConstants* constants,
                         LocalLexicalBudget budget);
[[nodiscard]] AcyclicResult WriteCapturedBranchBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    AcyclicBudget budget, const MethodConstants* constants,
    const BranchSourceFacts& facts,
    StructuredExit exit = StructuredExit::kReturn);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_BRANCH_SOURCE_FACTS_H_
