#ifndef ABCD_SOURCE_FAMILY_PRINT_H_
#define ABCD_SOURCE_FAMILY_PRINT_H_

#include "function_family.h"
#include "abcd/source/tree_printer.h"

namespace abcd::source {
class ClassTreeContext;
struct PrintedFamilyDraft {
  std::vector<PrintedFunction> members;
  std::vector<GeneratedRange> functions;
  std::size_t bytes = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Internal diagnostic syntax only, NOT executable publication. Requires fresh
// unchanged shared family/tree/capture products. Root/global/module enclosure,
// strictness and transitive premises remain unresolved. Inferred names use
// computed-property NamedEvaluation, never an invented lexical self binding.
// Trees lacking name evidence remain anonymous diagnostic syntax. Uses
// the SAME iterative printer as verified members, with per-owner event ranges.
// Every range uses absolute UTF-8 byte offsets within this entire draft.
// Realm expressions additionally require an actual destination scope even for
// drafts: there is no diagnostic placeholder spelling for a VM realm identity.
[[nodiscard]] std::variant<PrintedFamilyDraft, TreePrintError> PrintFamilyDraft(
    const ConstructedFamily& family, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr);
// Print an entry body in native module scope, with actual nested functions.
// Only the root drops its synthetic block/terminal return; descendants preserve
// receiver, return and coroutine syntax. This does not establish module bindings
// or class/capture completeness; callers retain those construction obligations.
[[nodiscard]] std::variant<PrintedFamilyDraft, TreePrintError> PrintModuleFamilyDraft(
    const ConstructedFamily& family, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr,
    std::optional<ModuleDefaultExport> default_export = {});
// Root is an actual class-body member; descendants remain ordinary/arrow
// expressions. This is not an object-literal method or standalone function.
[[nodiscard]] std::variant<PrintedFamilyDraft, TreePrintError> PrintClassFamilyDraft(
    const ConstructedFamily& family, const ClassTreeContext& context, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr, std::size_t indent = 0);
}  // namespace abcd::source
#endif
