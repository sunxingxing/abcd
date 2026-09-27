#include "tree_checks.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace abcd::source {
void CheckTreeOrigins(const FunctionTree& tree, TreeCheckBudget& budget) {
  using Error = TreeStructureError;
  std::size_t next_part = 0;
  for (std::size_t i = 0; i < tree.origins.size(); ++i) {
    budget.Step();
    std::visit([&](const auto& origin) {
      using T = std::decay_t<decltype(origin)>;
      if constexpr (std::is_same_v<T, CombinedOrigin>) {
        constexpr std::size_t kMinimumCombinedParts = 2;
        RequireTree(origin.first_part == next_part &&
                    origin.part_count >= kMinimumCombinedParts &&
                    origin.part_count <= tree.origin_parts.size() - next_part, Error::kOrigin);
        for (std::uint32_t p = 0; p < origin.part_count; ++p) {
          budget.Step();
          RequireTree(tree.origin_parts[next_part++].value < i, Error::kOrigin);
        }
      } else if constexpr (std::is_same_v<T, BindingOrigin>) {
        RequireTree(origin.symbol.value < tree.symbols.size(), Error::kOrigin);
      } else {
        static_assert(std::is_same_v<T, OriginalSite> ||
                      std::is_same_v<T, ElidedOrigin> || std::is_same_v<T, EdgeOrigin> ||
                      std::is_same_v<T, LoopOrigin> || std::is_same_v<T, EntryOrigin> ||
                      std::is_same_v<T, TryOrigin>);
        // Input instruction/edge identities are checked against the admitted IR
        // by semantic verification, not guessed from the syntax arena size.
      }
    }, tree.origins[i]);
  }
  RequireTree(next_part == tree.origin_parts.size(), Error::kOrigin);
  for (const auto& anchor : tree.anchors) {
    budget.Step();
    RequireTree(anchor.origin.value < tree.origins.size(), Error::kOrigin);
    if (const auto* boundary = std::get_if<BlockBoundary>(&anchor.position)) {
      RequireTree(boundary->block.value < tree.statements.size(), Error::kRange);
      const auto* block = std::get_if<BlockStmt>(&tree.statements[boundary->block.value].statement);
      RequireTree(block && boundary->index <= block->child_count, Error::kOrigin);
    }
  }
  std::vector<std::uint8_t> anchors;
  std::vector<analysis::OpId> operations;
  budget.Admit(1, sizeof(anchors) + sizeof(operations));
  budget.Array(anchors, tree.anchors.size());
  RequireTree(tree.signature.size() <= std::numeric_limits<std::size_t>::max() - tree.operations.size(),
              Error::kResourceLimit);
  budget.Array(operations, tree.operations.size() + tree.signature.size());
  for (std::size_t i = 0; i < tree.operations.size(); ++i) {
    budget.Step();
    const auto& placement = tree.operations[i];
    operations[i] = placement.operation;
    std::visit([&](const auto& node) {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, ExprId>)
        RequireTree(node.value < tree.expressions.size(), Error::kRange);
      else if constexpr (std::is_same_v<T, StmtId>)
        RequireTree(node.value < tree.statements.size(), Error::kRange);
      else if constexpr (std::is_same_v<T, PropertyId>)
        RequireTree(node.value < tree.properties.size(), Error::kRange);
      else {
        static_assert(std::is_same_v<T, AnchorId>);
        RequireTree(node.value < anchors.size() && !anchors[node.value], Error::kOwnership);
        anchors[node.value] = 1;
      }
    }, placement.node);
  }
  for (std::size_t i = 0; i < tree.signature.size(); ++i) {
    budget.Step();
    const auto& placement = tree.signature[i];
    RequireTree(placement.parameter.value < tree.parameters.size(), Error::kRange);
    RequireTree(tree.parameters[placement.parameter.value].initializer.has_value(), Error::kOrigin);
    RequireTree(placement.origin.value < tree.origins.size() &&
                std::holds_alternative<OriginalSite>(tree.origins[placement.origin.value]), Error::kOrigin);
    operations[tree.operations.size() + i] = placement.operation;
  }
  std::sort(operations.begin(), operations.end(), [&](auto left, auto right) {
    budget.Step(); return left < right;
  });
  for (std::size_t i = 1; i < operations.size(); ++i) {
    budget.Step(); RequireTree(operations[i - 1] != operations[i], Error::kOwnership);
  }
  for (const auto seen : anchors) { budget.Step(); RequireTree(seen, Error::kOwnership); }
}
}  // namespace abcd::source
