#ifndef ABCD_SOURCE_MODULE_INITIALIZATION_H_
#define ABCD_SOURCE_MODULE_INITIALIZATION_H_

#include "abcd/analysis/semantic_validation.h"
#include "module_slot_set.h"

namespace abcd::source {
struct ModuleCreationState {
  analysis::InstructionId creation;
  ModuleSlotSet slots;
};
struct ModuleCreationStates {
  std::vector<ModuleCreationState> states;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ModuleInitializationError { kInput, kWorkLimit, kResourceLimit };
// Normal owner only: successful Language stores make initialization monotone.
// Original normal SSA/proof are unchanged; no exception/resume edge is omitted.
// Complete sparse IDF + Boolean meet + dominator snapshots handle all branches
// and loops. Keys in the abstract RegisterSite program map only to ModuleSlot,
// never actual VM registers or source symbols. No dense slot x creation reads.
[[nodiscard]] std::variant<ModuleCreationStates, ModuleInitializationError>
BuildModuleCreationStates(const analysis::SemanticMethod& input,
                          const analysis::SemanticProof& proof,
                          const ModuleSlotSet& entry,
                          analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
