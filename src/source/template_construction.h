#ifndef ABCD_SOURCE_TEMPLATE_CONSTRUCTION_H_
#define ABCD_SOURCE_TEMPLATE_CONSTRUCTION_H_
#include "abcd/analysis/value_ir.h"
#include "abcd/source/source_contract.h"
#include "tree_checks.h"
namespace abcd::source {
struct TemplateQuasiSource {
  analysis::OpId raw;
  std::optional<analysis::OpId> cooked;
  bool operator==(const TemplateQuasiSource&) const = default;
};
struct TemplateExceptionPort {
  analysis::OpId operation;
  analysis::OriginId origin;
  analysis::LanguageEffect before_throw;
  std::optional<analysis::ValueHandler> handler;
  bool operator==(const TemplateExceptionPort&) const = default;
};
struct TemplateConstruction {
  analysis::OpId operation, call;
  analysis::UseId argument;
  std::vector<TemplateQuasiSource> quasis;
  std::vector<analysis::OpId> events;
  std::vector<TemplateExceptionPort> exceptions;
  bool operator==(const TemplateConstruction&) const = default;
};
struct TemplateConstructions {
  std::vector<TemplateConstruction> sites;
  std::vector<std::optional<analysis::OpId>> owners;
  std::vector<std::uint8_t> hidden;
};
[[nodiscard]] bool IsTemplateObject(const analysis::ValueOp&);
[[nodiscard]] TemplateConstructions BuildTemplateConstructions(
    const analysis::ValueProgram&, std::span<const analysis::ValueId> aliases,
    SourceContract, TreeCheckBudget&);
[[nodiscard]] bool VerifyTemplateConstructions(
    const analysis::ValueProgram&, std::span<const analysis::ValueId>,
    SourceContract, const TemplateConstructions&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
