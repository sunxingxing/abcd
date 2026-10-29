#ifndef ABCD_ANALYSIS_MODULE_INITIALIZATION_H_
#define ABCD_ANALYSIS_MODULE_INITIALIZATION_H_
#include "abcd/analysis/module_slot_set.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/exception_flow.h"
#include "abcd/analysis/register_value_flow.h"
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
// Exception-aware owner adapter for the method that owns the record's own
// bindings (the module entry). One site per ExceptionFlow node: a store takes
// effect at its Commit node and a local load snapshots at its Before node.
// Exceptional arcs leave Before nodes, so a handler (and any join after it)
// sees the state before the throwing instruction. Returns the sorted local
// loads whose slot is initialized on every path. As for inherited premises,
// a load immediately followed by its own HoleGuard keeps the guard-pair
// proof; store operands remain independently verified Language.
[[nodiscard]] std::variant<std::vector<LanguageLoadFact>, ModuleInitializationError>
ProveEntryModuleLoads(const ExceptionFlow& flow, SemanticBudget budget = {});
// Module slots initialized on every path (normal and exceptional arcs) before
// each creation's Before node, starting from the creator's own entry set.
// Sorted by flow instruction. Candidates only: store operands are verified
// Language by the owner's module writes. O(N) nodes plus the solver's cost.
[[nodiscard]] std::variant<ModuleInitialization, ModuleInitializationError>
ExceptionCreationModuleStates(const ExceptionFlow& flow, const ModuleSlotSet& entry, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
