#ifndef ABCD_SOURCE_PARAMETER_EXPRESSION_TREE_H_
#define ABCD_SOURCE_PARAMETER_EXPRESSION_TREE_H_
#include "abcd/source/source_tree.h"
#include "expression_constants.h"
#include "parameter_expression.h"
namespace abcd::source {
class DefaultBody;
struct DefaultSignatureContext;
class TreeCheckBudget;
class ParameterTreeBuilder;
class ParameterExpressionSource {
 public:
  const auto& Input() const { return *input_; }
  const auto& Certificate() const { return certificate_; }
  const auto& Operations() const { return operations_; }
  const auto& Roots() const { return roots_; }
  const auto& Names() const { return names_; }
  const auto& Bindings() const { return bindings_; }

 private:
  friend class ParameterTreeBuilder;
  ParameterExpressionSource() = default;
  std::shared_ptr<const analysis::ValueProgram> input_;
  ParameterExpressionCertificate certificate_;
  std::vector<std::optional<ExprId>> operations_, roots_;
  std::vector<std::optional<std::u16string>> names_;
  std::vector<std::optional<SymbolId>> bindings_;
};
void BuildParameterExpressionSignature(const DefaultBody&, FunctionTree&,
                                       const DefaultSignatureContext&,
                                       TreeCheckBudget&);
void VerifyParameterExpressionTree(const FunctionTree&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
