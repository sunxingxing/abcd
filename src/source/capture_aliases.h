#ifndef ABCD_SOURCE_CAPTURE_ALIASES_H_
#define ABCD_SOURCE_CAPTURE_ALIASES_H_

#include "capture_tree.h"
#include "function_syntax.h"

namespace abcd::source {
struct CaptureAliases {
  std::vector<CaptureSyntax> aliases;
  std::vector<std::u16string> names;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Actual parent cell symbols at an actual FunctionExpr or class-root anchor.
// A class anchor checks genuine layout/occurrence identity, not yet enclosing
// lexical visibility or transitive writes; conditional syntax is not emission.
// The caller
// supplies unchanged checked CaptureCells and parent tree/flow products, not
// assumed equality between parent/child CellIds or source SymbolIds. Child
// spelling is copied exactly; no scalar snapshot or new declaration is made.
// Append names at first_name, then independently check the constructed child
// with VerifyCaptureTree. An unresolved outward target stays unsupported.
[[nodiscard]] std::variant<CaptureAliases, CaptureTreeError> BindCaptureAliases(
    const CaptureCells& cells, CaptureTree parent, FunctionOwnerId child,
    NameId first_name = {}, analysis::SemanticBudget budget = {},
    const ClassCaptureAnchor* class_anchor = nullptr);
}  // namespace abcd::source
#endif
