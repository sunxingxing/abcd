#ifndef ABCD_SOURCE_MODULE_ALIASES_H_
#define ABCD_SOURCE_MODULE_ALIASES_H_

#include "evaluation_plan.h"
#include "function_syntax.h"
#include "function_values.h"
#include "module_reads.h"
#include "abcd/source/record_module_context.h"

namespace abcd::source {
struct ModuleAliases {
  RouteInputIdentity identity;
  abc::EntityOffset method;
  abc::EntityOffset record;
  ModuleOwnerId owner;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ModuleEvaluation> evaluations;
  std::vector<ModuleSyntax> aliases;
  // Append at first_name in the caller's FunctionSyntax name arena. Names are
  // exact metadata spellings, not generated aliases or executable source.
  NameId first_name;
  std::vector<std::u16string> names;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ModuleAliasError : std::uint8_t {
  kIdentity, kCoverage, kGuardName, kResourceLimit, kWorkLimit
};
struct ModuleAliasFailure {
  std::variant<ModuleAliasError, ModuleGuardError, ModuleReadFailure,
      abc::MetadataError> cause;
  std::optional<abc::CodeOffset> pc;
};
// Re-resolve the actual record table and guarded/initialized read proof. Source
// symbols use canonical record binding order: external slots, then local slots
// (NOT local-export rows). Thus siblings agree independently of which bindings
// they read. The caller owns the record->ModuleOwnerId assignment.
// Every returned alias is read-only: local exports do not establish source
// declaration kind or write permission. This never freezes a live value.
// This binds metadata/name/evaluation obligations only. It is NOT a TDZ,
// cyclic-initialization, scope, dependency or executable-publication proof.
// File/index/input/verified working IR/context are unchanged and caller-admitted.
[[nodiscard]] std::variant<ModuleAliases, ModuleAliasFailure> BindModuleAliases(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const FunctionInput& input,
    const FunctionValues& values, const RecordModuleContext& context,
    ModuleOwnerId owner, NameId first_name = {}, analysis::SemanticBudget budget = {});

struct PhasedModuleInput {
  const RouteInputIdentity& identity;
  abc::EntityOffset method;
  abc::EntityOffset record;
  const analysis::PhasedValueInput& phases;
  const analysis::ValueProgram& graph;
  const ModuleGuards& guards;
};
// Caller-owned record table and file/index, reused across sibling methods.
// Do not rediscover packaging or scan every record for every method.
struct PhasedModuleBinding {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  const RecordModuleContext& record;
  ModuleOwnerId owner;
};
// Same metadata binder, but original pair correspondence is phase-aware.
// Input identity/body/callable ownership and record publication remain owner
// obligations; this adapter neither fabricates a FunctionInput nor drops phases.
[[nodiscard]] std::variant<ModuleAliases, ModuleAliasFailure> BindPhasedModuleAliases(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const PhasedModuleInput& input,
    const RecordModuleContext& context, ModuleOwnerId owner, NameId first_name = {},
    analysis::SemanticBudget budget = {});

enum class ModuleAliasTreeError : std::uint8_t {
  kIdentity, kRevision, kCoverage, kTarget, kName, kAccess,
  kResourceLimit, kWorkLimit
};
// Like capture/tree correspondence, consumes fresh unchanged binding evidence
// and a tree/evaluation pair that passed shared structural/semantic checks.
// Checks the ACTUAL aliases and evaluation targets, not just their input syntax.
// Does not certify the record owner assignment or module initialization order.
[[nodiscard]] std::variant<std::uint64_t, ModuleAliasTreeError> VerifyModuleAliasTree(
    const ModuleAliases& bindings, const EvaluationPlan& evaluation,
    const FunctionTree& tree, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
