#include <type_traits>

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
bool ReadsAccumulator(const ScalarExpression& expression) {
  const auto reads = [](const ScalarValue& value) {
    return std::holds_alternative<Accumulator>(value);
  };
  return std::visit(
      [&](const auto& node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, ScalarValue>) {
          return reads(node);
        } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
          return reads(node.input);
        } else if constexpr (std::is_same_v<Node, BinaryExpression>) {
          return reads(node.left) || reads(node.right);
        } else {
          static_assert(std::is_same_v<Node, PropertyRead>);
          return reads(node.receiver) || reads(node.key);
        }
      },
      expression);
}
}  // namespace abcd::analysis
