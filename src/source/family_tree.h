#ifndef ABCD_SOURCE_FAMILY_TREE_H_
#define ABCD_SOURCE_FAMILY_TREE_H_

#include "family_file.h"
#include "derived_projection.h"
#include "cell_defaults.h"
#include "cell_default_proof.h"
#include "function_abi.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "syntax_resolution.h"
#include "default_signature.h"
#include "tree_semantics.h"
#include "family_enclosure.h"
#include "family_writes.h"

namespace abcd::source {
struct ExternalFamilyCapture {
  const FileFunction& creator;
  ClassCaptureParent source;
};
enum class FamilyTreeError : std::uint8_t {
  kContext, kParent, kModules, kResourceLimit, kWorkLimit, kDeferredCell
};
struct FamilyTreeFailure {
  FamilyId member;
  std::variant<FamilyTreeError, DerivedProjectionError, SyntaxResolutionFailure, ControlError,
      EvaluationError, FunctionTreeFailure, TreeFlowError, FunctionAbiError,
      ModuleAliasFailure, ModuleAliasTreeError, FamilyError,
      ParameterPlanError, CellDefaultError, CellDefaultProofError, DefaultBodyError,
      analysis::ValueEnvironmentError, CellError, ModuleGuardError, CaptureCellFailure,
      CaptureEvaluationFailure, DefaultSignatureError, TreeSemanticError,
      FamilyEnclosureFailure, FamilyWriteFailure, InstructionSourceError, DefaultCandidateError, ArrayInitializationError> cause;
  // Discovery evidence only; no source initializer or erasure is authorized.
  std::optional<std::size_t> cell_default_candidates;
  bool cell_defaults_proved = false;
  // Resolved while the owned family is still alive; never a creation PC.
  std::optional<abc::EntityOffset> method;
};
// Consumes a fresh, unchanged ReadFunctionFamily result and actual root context.
// Parent-first shared construction checks each incoming capture against the
// actual parent tree. Complete-family write domains are checked against original
// stores and residual trees, without granting immutability. No source receipt:
// root enclosure and global/module initialization remain publication gates.
// Proven value/cell literal defaults project before whole-family edge rebinding;
// mixed or effectful default prologues still require their own proof.
// Phase roots use the same member AST/ABI/source pipeline and require lookup
// from the input snapshot. Retain child contexts for post-projection rebinding.
// Existing file/index/context/family storage is caller-admitted. This allowance
// covers all additional construction/scratch; conservative accounting retains
// previous pass charges even after their storage is released.
// AllowPartial currently permits a capture-free leaf's straight-line body prefix;
// no ABI/source census or whole-family write proof is manufactured for a gap.
[[nodiscard]] std::variant<ConstructedFamily, FamilyTreeFailure> BuildFileFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, const FunctionContext& root,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr,
    ModuleOwnerId module_owner = {}, const abc::MethodLookup* lookup = nullptr,
    RecoveryMode recovery = RecoveryMode::kCompleteOnly,
    const ExternalFamilyCapture* external = nullptr);
// Reuse a record adapter's root tree and checked flow, then construct all actual
// children with the same defaults/capture/ABI pipeline. No fictional creation
// context or function parameters are supplied for the module entry. Input and
// seed storage are caller-admitted. Root class obligations require actual ClassExpr
// bodies; nested class families still require their own capture composition.
[[nodiscard]] std::variant<ConstructedFamily, FamilyTreeFailure> BuildRecordFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, FamilyMember entry,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr,
    ModuleOwnerId module_owner = {}, const abc::MethodLookup* lookup = nullptr);
}  // namespace abcd::source
#endif
