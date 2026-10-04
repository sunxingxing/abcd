#ifndef ABCD_ANALYSIS_LOCAL_SSA_H_
#define ABCD_ANALYSIS_LOCAL_SSA_H_

#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/capture_effect.h"
#include "abcd/analysis/instruction_rank.h"

namespace abcd::analysis {
enum class LocalSsaError { kControlFlow, kEnvironment, kInstruction, kResourceLimit };
enum class LocalReadState : std::uint8_t { kUnreachable, kPreserved, kExposed, kCapture };
struct LocalSsa {
  // RegisterId here indexes local cells, NOT VM registers. InstructionValue
  // and read coordinates still index the original unchanged body. Entries are
  // unknown initial cell contents; a store definition denotes its VM input.
  RegisterSsa values;
  // An unpreserved creation witness, never a temporal boundary for reads.
  std::optional<InstructionId> unpreserved_capture;
  // True only before any reachable unpreserved creation. Original site order
  // is a coordinate, not a temporal proof across different CFG blocks.
  std::vector<LocalReadState> read_states;
  // Reachable DAG linearization, including order inside each block. A lower
  // rank proves a site cannot execute after a higher-ranked site. It does not
  // prove dominance or permission to reorder operations. No rank for dead sites.
  std::vector<std::optional<InstructionRank>> site_order;
  std::uint64_t work_units = 0;
  [[nodiscard]] bool PreservesRead(InstructionId site) const {
    return site.value < read_states.size() && read_states[site.value] == LocalReadState::kPreserved;
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return values.AllocatedBytes() + read_states.capacity() * sizeof(LocalReadState) +
        site_order.capacity() * sizeof(std::optional<InstructionRank>);
  }
};

// Sparse cell SSA for an unchanged acyclic normal CFG. Each allocation
// executes at most once, so distinct environment/slot pairs are distinct SSA
// bindings without synthetic allocation writes. No handlers or actual cycles;
// physical backward jumps are permitted by the common graph ordering proof.
// Sorted same-body preserved certificates exempt proven read-only creations.
// Consumers query PreservesRead: the graph records
// explicit stores only, NOT closure writes. Captured incoming cells are not
// promoted. Unsupported semantic operations and exposed environment identities
// reject rather than assuming they preserve local storage.
// registers is the unchanged body's register SSA. Allocation accumulator
// definitions must have no reads or phi uses, including private copies.
[[nodiscard]] std::variant<LocalSsa, LocalSsaError> BuildLocalSsa(
    const abc::MethodBody& body, const NormalFlow& graph,
    const RegisterSsa& registers, std::uint64_t frame_size,
    abc::ProfileId profile, IdfBudget budget,
    std::uint64_t work, std::span<const CapturePreservation> preserved = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_LOCAL_SSA_H_
