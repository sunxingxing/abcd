#ifndef ABCD_SOURCE_MODULE_DEFAULT_EXPORT_H_
#define ABCD_SOURCE_MODULE_DEFAULT_EXPORT_H_

#include "abcd/source/tree_printer.h"

namespace abcd::source {
struct ConstructedFamily;
struct DefaultExportCheck {
  bool eligible = false;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Only syntax/lifetime eligibility. The record adapter separately proves the
// wire sentinel, sole default alias and original first-store declaration.
// Refuse partial descendants, references/updates/captures and direct anonymous
// function/class initializers (default NamedEvaluation needs its own proof).
[[nodiscard]] std::variant<DefaultExportCheck, TreePrintError> CheckModuleDefaultExport(
    const FunctionTree& root, const ConstructedFamily* family,
    ModuleDefaultExport candidate, analysis::SemanticBudget budget = {});
}
#endif
