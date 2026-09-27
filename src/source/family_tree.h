#ifndef ABCD_SOURCE_FAMILY_TREE_H_
#define ABCD_SOURCE_FAMILY_TREE_H_

#include "family_file.h"
#include "cell_defaults.h"
#include "cell_default_proof.h"
#include "function_abi.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "syntax_resolution.h"
#include "default_signature.h"
#include "tree_semantics.h"
#include "family_enclosure.h"

namespace abcd::source {
enum class FamilyTreeError : std::uint8_t {
  kContext, kParent, kModules, kResourceLimit, kWorkLimit
};
struct FamilyTreeFailure {
  FamilyId member;
  std::variant<FamilyTreeError, SyntaxResolutionFailure, ControlError,
      EvaluationError, FunctionTreeError, TreeFlowError, FunctionAbiError,
      ModuleAliasFailure, ModuleAliasTreeError, FamilyError,
      ParameterPlanError, CellDefaultError, CellDefaultProofError, DefaultBodyError,
      analysis::ValueEnvironmentError, CellError, ModuleGuardError, CaptureCellError,
      CaptureEvaluationFailure, DefaultSignatureError, TreeSemanticError,
      FamilyEnclosureFailure> cause;
  // Discovery evidence only; no source initializer or erasure is authorized.
  std::optional<std::size_t> cell_default_candidates;
  bool cell_defaults_proved = false;
};
// Consumes a fresh, unchanged ReadFunctionFamily result and actual root context.
// Parent-first shared construction checks each incoming capture against the
// actual parent tree. No source receipt: root enclosure, transitive writes,
// global/module initialization remain publication gates. Cell-default projection
// runs before whole-family edge rebinding; other default forms still refuse.
// Existing file/index/context/family storage is caller-admitted. This allowance
// covers all additional construction/scratch; conservative accounting retains
// previous pass charges even after their storage is released.
[[nodiscard]] std::variant<ConstructedFamily, FamilyTreeFailure> BuildFileFamilyTrees(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, FileFamily family, const FunctionContext& root,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr,
    ModuleOwnerId module_owner = {});
}  // namespace abcd::source
#endif
