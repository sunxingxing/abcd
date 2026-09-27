#ifndef ABCD_SOURCE_FAMILY_PRINT_H_
#define ABCD_SOURCE_FAMILY_PRINT_H_

#include "function_family.h"
#include "abcd/source/tree_printer.h"

namespace abcd::source {
struct PrintedFamilyDraft {
  std::vector<PrintedFunction> members;
  std::vector<GeneratedRange> functions;
  std::size_t bytes = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Internal diagnostic syntax only, NOT executable publication. Requires fresh
// unchanged shared family/tree/capture products. Root/global/module enclosure,
// callable names and transitive premises remain unresolved. Prints an anonymous
// function expression, not a declaration with an invented self binding. Uses
// the SAME iterative printer as verified members, with per-owner event ranges.
// Every range uses absolute UTF-8 byte offsets within this entire draft.
[[nodiscard]] std::variant<PrintedFamilyDraft, TreePrintError> PrintFamilyDraft(
    const ConstructedFamily& family, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {});
}  // namespace abcd::source
#endif
