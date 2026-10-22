#ifndef ABCD_SOURCE_TEMPLATE_TREE_H_
#define ABCD_SOURCE_TEMPLATE_TREE_H_
#include "template_construction.h"
namespace abcd::source {
struct EvaluationPlan;
struct ExpressionConstants;
struct OriginalFlow;
struct TemplateQuasiText {
  std::u16string raw;
  std::optional<std::u16string> cooked;
};
struct TemplateSiteText {
  std::vector<TemplateQuasiText> quasis;
};
struct OriginalTemplates {
  analysis::ValueProgram input;
  std::vector<analysis::ValueId> aliases;
  TemplateConstructions proof;
  std::vector<TemplateSiteText> texts;
};
void RetainTemplateInputs(const analysis::ValueProgram&, const EvaluationPlan&,
                          const FunctionTree&, ExpressionConstants,
                          OriginalFlow&, TreeCheckBudget&);
void VerifyOriginalTemplates(const FunctionTree&, const OriginalFlow&,
                             TreeCheckBudget&);
void VerifyTemplateCalls(const FunctionTree&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
