#include "abcd/source/source_tree.h"

#include <type_traits>

#include "parameter_expression_tree.h"
#include "tree_print_trace.h"

namespace abcd::source {
std::variant<TreeStorage, TreeStorageError> MeasureTreeStorage(
    const FunctionTree& tree, analysis::SemanticBudget budget) {
  TreePrintSiteScope site(TreePrintSite::kTreeStorage);
  auto remaining = budget.bytes;
  auto work = budget.work;
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > remaining / width) { PrintByteCharge(count,width,remaining,budget.bytes,budget.bytes-remaining); return false; }
    remaining -= count * width;
    return true;
  };
  const auto arena = [&](const auto& items) {
    using T = typename std::decay_t<decltype(items)>::value_type;
    return admit(items.capacity(), sizeof(T));
  };
  if (!admit(1, sizeof(FunctionTree)) || !arena(tree.capture_requirements) ||
      !arena(tree.parameters) || !arena(tree.captures) ||
      !arena(tree.modules) || !arena(tree.scopes) || !arena(tree.symbols) ||
      !arena(tree.names) || !arena(tree.texts) || !arena(tree.expressions) ||
      !arena(tree.properties) || !arena(tree.arguments) ||
      !arena(tree.statements) || !arena(tree.receiver_guards) ||
      !arena(tree.children) || !arena(tree.closure_captures) ||
      !arena(tree.origins) || !arena(tree.origin_parts) ||
      !arena(tree.anchors) || !arena(tree.operations) ||
      !arena(tree.signature) || !arena(tree.recovery_issues) ||
      !arena(tree.labels) || !arena(tree.finalizers) ||
      !arena(tree.yield_returns))
    return TreeStorageError::kResourceLimit;
  if(tree.concurrent&&!admit(tree.concurrent->AllocatedBytes(),1))return TreeStorageError::kResourceLimit;
  const auto payloads = [&](const auto& strings) -> std::optional<TreeStorageError> {
    for (const auto& text : strings) {
      if (work == 0) { PrintCharge(TreePrintBudgetKind::kWork,1,work,budget.work); return TreeStorageError::kWorkLimit; }
      --work;
      // Include the null terminator without overflowing capacity + 1.
      if (!admit(text.capacity(), sizeof(char16_t)) || !admit(1, sizeof(char16_t)))
        return TreeStorageError::kResourceLimit;
    }
    return {};
  };
  if (const auto error = payloads(tree.names)) return *error;
  if (const auto error = payloads(tree.texts)) return *error;
  if (tree.parameter_expressions) {
    const auto& s = *tree.parameter_expressions;
    const auto& g = s.Input();
    const auto& c = s.Certificate();
    if (!admit(1, sizeof(ParameterExpressionSource)) ||
        !admit(1, sizeof(analysis::ValueProgram)) || !arena(g.blocks) ||
        !arena(g.edges) || !arena(g.ops) || !arena(g.values) ||
        !arena(g.uses) || !arena(g.origins) || !arena(g.exceptional_exits) ||
        !arena(g.input.sites) || !arena(g.input.values) || !arena(g.phases) ||
        !arena(c.defaults) || !arena(c.events) || !arena(c.exceptions) ||
        !arena(s.Operations()) || !arena(s.Roots()) || !arena(s.Names()) ||
        !arena(s.Bindings()))
      return TreeStorageError::kResourceLimit;
    for (const auto& text : s.Names()) {
      if (!work) return TreeStorageError::kWorkLimit;
      --work;
      if (text && (!admit(text->capacity(), sizeof(char16_t)) ||
                   !admit(1, sizeof(char16_t))))
        return TreeStorageError::kResourceLimit;
    }
  }
  return TreeStorage{budget.bytes - remaining, budget.work - work};
}
}  // namespace abcd::source
