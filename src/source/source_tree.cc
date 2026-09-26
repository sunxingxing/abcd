#include "abcd/source/source_tree.h"

#include <type_traits>

namespace abcd::source {
std::variant<TreeStorage, TreeStorageError> MeasureTreeStorage(
    const FunctionTree& tree, analysis::SemanticBudget budget) {
  auto remaining = budget.bytes;
  auto work = budget.work;
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > remaining / width) return false;
    remaining -= count * width;
    return true;
  };
  const auto arena = [&](const auto& items) {
    using T = typename std::decay_t<decltype(items)>::value_type;
    return admit(items.capacity(), sizeof(T));
  };
  if (!admit(1, sizeof(FunctionTree)) || !arena(tree.parameters) || !arena(tree.captures) ||
      !arena(tree.scopes) || !arena(tree.symbols) || !arena(tree.names) ||
      !arena(tree.texts) || !arena(tree.expressions) || !arena(tree.properties) || !arena(tree.arguments) || !arena(tree.statements) ||
      !arena(tree.children) || !arena(tree.closure_captures) || !arena(tree.origins) || !arena(tree.origin_parts) ||
      !arena(tree.anchors) || !arena(tree.operations) || !arena(tree.signature))
    return TreeStorageError::kResourceLimit;
  const auto payloads = [&](const auto& strings) -> std::optional<TreeStorageError> {
    for (const auto& text : strings) {
      if (work == 0) return TreeStorageError::kWorkLimit;
      --work;
      // Include the null terminator without overflowing capacity + 1.
      if (!admit(text.capacity(), sizeof(char16_t)) || !admit(1, sizeof(char16_t)))
        return TreeStorageError::kResourceLimit;
    }
    return {};
  };
  if (const auto error = payloads(tree.names)) return *error;
  if (const auto error = payloads(tree.texts)) return *error;
  return TreeStorage{budget.bytes - remaining, budget.work - work};
}
}  // namespace abcd::source
