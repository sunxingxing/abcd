#ifndef ABCD_TESTS_EXCEPTION_FAMILY_PROBE_H_
#define ABCD_TESTS_EXCEPTION_FAMILY_PROBE_H_

#include "../../src/source/phased_body.h"
#include "../../src/source/function_values.h"
#include "../../src/source/capture_cells.h"
#include "../../src/source/family_enclosure.h"
#include "../../src/source/module_aliases.h"
#include "../../src/source/family_writes.h"
#include "../../src/source/phased_function_file.h"

namespace abcd::test {
struct ExceptionChild {
  source::FunctionContext context;
  abc::MethodBody body;
  source::FunctionInput input;
  source::FunctionValues values;
  source::CaptureCells captures;
  const source::PhasedCreation& creation;
};
enum class FamilyProbeStage : std::uint8_t {
  kParentSyntax, kParentTree, kParentFlow, kChildSyntax, kChildModules,
  kChildControl, kChildEvaluation, kChildTree, kChildFlow, kChildAliases,
  kLayout, kEnclosure, kWrites, kPrint, kDone
};
struct ExceptionFamilyProbe {
  FamilyProbeStage stage = FamilyProbeStage::kParentSyntax;
  bool parent_tree = false;
  std::size_t child_trees = 0;
  bool layout = false;
  bool enclosure = false;
  std::optional<source::FunctionTreeError> tree_error;
  bool rejected_capture_forgery = false;
  std::size_t checked_store_methods = 0, checked_lexical_stores = 0;
  bool family_writes = false, rejected_write_forgery = false;
  std::size_t write_creations = 0;
  std::size_t abi_methods = 0, source_methods = 0;
  bool rejected_source_forgery = false;
  std::size_t mapped_instructions = 0;
  std::optional<std::string> draft;
};
// Real decoded inputs and checked capture edges, not invented syntax. This
// diagnostic composes production builders but grants no publication receipt.
ExceptionFamilyProbe ProbeExceptionFamily(const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const source::RecordModuleContext& modules, source::ModuleOwnerId module_owner,
    const source::FunctionContext& context, const source::FilePhasedFunction& input,
    const source::EvaluationPlan& evaluation,
    std::vector<ExceptionChild> children, const abc::MethodLookup& lookup,
    analysis::SemanticBudget budget);
}  // namespace abcd::test
#endif
