#ifndef ABCD_ANALYSIS_LOCAL_SSA_H_
#define ABCD_ANALYSIS_LOCAL_SSA_H_

#include "abcd/analysis/register_ssa.h"

namespace abcd::analysis {
enum class LocalSsaError { kControlFlow, kEnvironment, kInstruction, kResourceLimit };
struct LocalSsa {
  // RegisterId here indexes local cells, NOT VM registers. InstructionValue
  // and read coordinates still index the original unchanged body. Entries are
  // unknown initial cell contents; a store definition denotes its VM input.
  RegisterSsa values;
  std::optional<InstructionId> first_capture;
  std::uint64_t work_units = 0;
};

// Sparse cell SSA for an unchanged forward-only normal CFG. Each allocation
// executes at most once, so distinct environment/slot pairs are distinct SSA
// bindings without synthetic allocation writes. No handlers or backward edges.
// Consumers must invalidate loads at/after first_capture: the graph records
// explicit stores only, NOT closure writes. Captured incoming cells are not
// promoted. Unsupported semantic operations and exposed environment identities
// reject rather than assuming they preserve local storage.
// registers is the unchanged body's register SSA. Allocation accumulator
// definitions must have no reads or phi uses, including private copies.
[[nodiscard]] std::variant<LocalSsa, LocalSsaError> BuildLocalSsa(
    const abc::MethodBody& body, const NormalFlow& graph,
    const RegisterSsa& registers, std::uint64_t frame_size,
    abc::ProfileId profile, IdfBudget budget,
    std::uint64_t work);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_LOCAL_SSA_H_
