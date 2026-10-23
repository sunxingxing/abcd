#ifndef ABCD_SOURCE_MODULE_GUARDS_H_
#define ABCD_SOURCE_MODULE_GUARDS_H_

#include "function_input.h"
#include "abcd/analysis/value_ir.h"

namespace abcd::analysis { struct PhasedValueInput; }
namespace abcd::source {
struct ParameterExpressionInput;
struct ModuleReadSite {
  analysis::OpId read;
  // Absence is an initialized local read, never an omitted required check.
  // Original input owner evidence independently authorizes this alternative.
  std::optional<analysis::OpId> guard;
  bool operator==(const ModuleReadSite&) const = default;
};
// The ops strictly between a module read and its Hole guard, if any, are pure
// register copies (ValueCopy without effects or throw) in the same block, with
// contiguous orders. Such copies observe nothing and cannot throw, so the guard
// still checks the read's value at the read's position. Cost is the gap length.
[[nodiscard]] bool CopyOnlyModuleGuardInterval(const analysis::ValueProgram&,
                                               analysis::OpId read, analysis::OpId guard);
struct ModuleGuards {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  // Original proof order. No entry means no module-read lowering permission.
  std::vector<ModuleReadSite> pairs;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  std::shared_ptr<const ParameterExpressionInput> signature;
  std::vector<ModuleReadSite> signature_pairs;
};
enum class ModuleGuardError : std::uint8_t {
  kRevision, kOrigin, kCoverage, kRead, kGuard, kOrder, kEffects,
  kUnsupportedProof, kResourceLimit, kWorkLimit, kReadProof
};
// Every effectful lazy read, including discarded results, needs a complete
// load/guard proof. O(operations + pairs), no allocation; shared by verifiers.
[[nodiscard]] std::variant<std::uint64_t, ModuleGuardError> VerifyLazyModuleReads(
    const analysis::ValueProgram&, const ModuleGuards&, analysis::SemanticBudget);
// Rebase immutable original guarded/initialized proofs onto working IR.
// Language result facts from a guarded load are conditional on the WHOLE pair;
// they never authorize removing the guard or exposing the raw pre-guard value.
// No module binding/TDZ/name equivalence or publication certificate is produced.
[[nodiscard]] std::variant<ModuleGuards, ModuleGuardError> BuildModuleGuards(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<ModuleGuards, ModuleGuardError>
BuildParameterModuleGuards(const FunctionInput&, const analysis::ValueProgram&,
                           std::shared_ptr<const ParameterExpressionInput>,
                           analysis::SemanticBudget = {});
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
