#ifndef ABCD_SOURCE_BINDING_PLAN_VIEW_H_
#define ABCD_SOURCE_BINDING_PLAN_VIEW_H_
#include "binding_initializer.h"
#include <span>
namespace abcd::source {
// One synchronous immutable candidate, including declaration/completion and
// value facets. Overlay owned arrays by replacing spans BEFORE analysis; no
// query reads a mixture of current and proposed plans. Derived effect facts
// and print prefix counts are deliberately not input capabilities.
struct BindingPlanView {
  std::span<const std::optional<BindingInitializer>> statements;
  std::span<const std::uint8_t> omitted;
  std::span<const std::optional<ExprId>> statement_values,forwarded_values;
  std::span<const std::optional<std::uint32_t>> loop_declarations;
  std::span<const TreeLiteral> primitives;
  std::span<const std::optional<PrimitiveId>> literals,read_literals;
  std::span<const std::optional<SymbolId>> read_bindings,read_sources,aliases,storage,undefined_stores;
  std::span<const std::optional<DeclarationValue>> declaration_values;
  std::span<const std::optional<DeclarationCompletion>> declaration_completions;
  std::span<const std::optional<ExprId>> replacements;
  std::span<const std::optional<ExpressionTransport>> transports;
  BindingPlanView(const BindingInitializers& plan):
      statements(plan.statements),omitted(plan.omitted),statement_values(plan.statement_values),
      forwarded_values(plan.forwarded_values),loop_declarations(plan.loop_declarations),primitives(plan.primitives),
      literals(plan.literals),read_literals(plan.read_literals),read_bindings(plan.read_bindings),
      read_sources(plan.read_sources),aliases(plan.aliases),storage(plan.storage),undefined_stores(plan.undefined_stores),
      declaration_values(plan.declaration_values),declaration_completions(plan.declaration_completions),
      replacements(plan.replacements),transports(plan.transports){}
  [[nodiscard]] SymbolId Storage(SymbolId binding) const {
    return BindingProjection{aliases,storage,read_bindings}.Storage(binding);
  }
  [[nodiscard]] SymbolId ReadBinding(SymbolId binding) const {
    return BindingProjection{aliases,storage,read_bindings}.Read(binding);
  }
  [[nodiscard]] SymbolId ReadBinding(ExprId expression,SymbolId binding) const {
    return BindingProjection{aliases,storage,read_bindings,read_sources}.Read(expression,binding);
  }
  [[nodiscard]] std::optional<PrimitiveId> ReadLiteral(ExprId expression,SymbolId binding) const {
    if(!read_literals.empty()&&read_literals[expression.value])return read_literals[expression.value];
    return literals[binding.value];
  }
};
}
#endif
