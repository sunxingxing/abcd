#ifndef ABCD_SOURCE_SWITCH_SYNTAX_H_
#define ABCD_SOURCE_SWITCH_SYNTAX_H_
#include "tree_checks.h"
namespace abcd::source {
struct SwitchSyntax {
  ExprId discriminant;
  SymbolId cache;
};
// Checks source factoring independently: every repeated discriminant is the
// same private initialized, non-captured binding and no test can write it.
[[nodiscard]] SwitchSyntax VerifySwitchSyntax(const FunctionTree&,
                                              const SwitchStmt&,
                                              TreeCheckBudget&);
void VerifyAllSwitchSyntax(const FunctionTree&, TreeCheckBudget&);
[[nodiscard]] const BinaryExpr& SwitchComparison(const FunctionTree&,
                                                 const SwitchCaseStmt&);
}  // namespace abcd::source
#endif
