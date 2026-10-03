#ifndef ABCD_ANALYSIS_MODULE_INITIALIZATION_H_
#define ABCD_ANALYSIS_MODULE_INITIALIZATION_H_
#include "abcd/analysis/module_slot_set.h"
#include "abcd/analysis/semantic_ir.h"
namespace abcd::analysis {
// Abstract effects of genuine normal-flow sites. IDs index the supplied
// program, not VM registers. Owners retain original or working provenance.
struct ModuleStateSite {
  abc::CodeOffset pc;
  std::optional<ModuleSlot> store;
  // Snapshot before this site; its successful store affects later sites.
  bool snapshot = false;
};
struct ModuleStateSnapshot {
  InstructionId instruction;
  ModuleSlotSet slots;
};
struct ModuleInitialization {
  std::vector<ModuleStateSnapshot> states;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ModuleInitializationError { kInput, kWorkLimit, kResourceLimit };
// Candidates, not permission: every reachable store operand must independently
// pass full Language verification before these roots authorize source reads or
// child premises. Entry false paths prevent cyclic reads/stores
// self-certifying. Complete sparse IDF + Boolean AND + persistent dominator
// snapshots; no dense slot-by-query read matrix and no
// entry-block/sequence/layout admission.
[[nodiscard]] std::variant<ModuleInitialization, ModuleInitializationError>
BuildModuleInitialization(std::span<const ModuleStateSite>, const NormalFlow&,
                          const ModuleSlotSet& entry,
                          SemanticBudget budget = {});
// Original instruction owner adapter; normal flow only. Full semantic input
// verification discharges all candidate-store obligations before publication.
[[nodiscard]] std::variant<ModuleInitialization, ModuleInitializationError>
BuildModuleInitialization(const SemanticMethod&, const ModuleSlotSet& entry,
                          SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
