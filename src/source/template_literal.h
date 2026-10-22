#ifndef ABCD_SOURCE_TEMPLATE_LITERAL_H_
#define ABCD_SOURCE_TEMPLATE_LITERAL_H_
#include "tree_checks.h"
namespace abcd::source {
// TRV is kept verbatim. Escaping a raw delimiter changes String.raw, so a
// sequence without an equivalent TemplateCharacters token must be refused.
[[nodiscard]] std::optional<std::u16string> CookTemplateRaw(std::u16string_view,
                                                            TreeCheckBudget&);
void VerifyTemplateLiteral(const TemplateObjectExpr&,
                           std::span<const std::u16string>, SourceContract,
                           TreeCheckBudget&);
}  // namespace abcd::source
#endif
