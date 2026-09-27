#ifndef ABCD_SOURCE_MODULE_GUARDS_H_
#define ABCD_SOURCE_MODULE_GUARDS_H_

#include "function_input.h"
#include "abcd/analysis/value_ir.h"

namespace abcd::analysis { struct PhasedValueInput; }
namespace abcd::source {
struct GuardedModuleRead {
  analysis::OpId read;
  analysis::OpId guard;
  bool operator==(const GuardedModuleRead&) const = default;
};
struct ModuleGuards {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  // Original proof order. No entry means no guarded-read lowering permission.
  std::vector<GuardedModuleRead> pairs;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ModuleGuardError : std::uint8_t {
  kRevision, kOrigin, kCoverage, kRead, kGuard, kOrder, kEffects,
  kUnsupportedProof, kResourceLimit, kWorkLimit
};
// Rebase immutable original pair proofs onto unchanged verified working IR.
// Language result facts from a guarded load are conditional on the WHOLE pair;
// they never authorize removing the guard or exposing the raw pre-guard value.
// No module binding/TDZ/name equivalence or publication certificate is produced.
[[nodiscard]] std::variant<ModuleGuards, ModuleGuardError> BuildModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, ModuleGuardError> VerifyModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const ModuleGuards& plan, analysis::SemanticBudget budget = {});
// Original exception routes must agree and the guard must have no bypass
// entry. This proves pair correspondence, not module-name/TDZ equivalence,
// exception snapshot relocation, evaluation scheduling or source admission.
[[nodiscard]] std::variant<ModuleGuards, ModuleGuardError> BuildPhasedModuleGuards(
    const analysis::PhasedValueInput& input, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, ModuleGuardError> VerifyPhasedModuleGuards(
    const analysis::PhasedValueInput& input, const analysis::ValueProgram& graph,
    const ModuleGuards& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
