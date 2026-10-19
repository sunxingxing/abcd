#ifndef ABCD_SOURCE_EXPRESSION_CHILDREN_H_
#define ABCD_SOURCE_EXPRESSION_CHILDREN_H_
#include "abcd/source/source_tree.h"
#include <type_traits>
namespace abcd::source {
// Visit immediate expression ownership on an already structurally checked tree.
// Variadic slots stream once; function captures are bindings, not evaluated children.
template <class Visit>
void VisitExpressionChildren(const FunctionTree& tree, const TreeExpression& expression, Visit visit) {
  const auto key = [&](const MemberKey& member) {
    if (const auto* computed = std::get_if<ComputedKey>(&member)) visit(computed->expression);
  };
  const auto arguments = [&](ArgumentRange range) {
    for (std::uint32_t i = 0; i < range.count; ++i) visit(tree.arguments[range.first + i]);
  };
  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, MemberExpr> || std::is_same_v<T, DeleteExpr>) { visit(node.base); key(node.key); }
    else if constexpr (std::is_same_v<T, UnaryExpr> || std::is_same_v<T, AwaitExpr> || std::is_same_v<T, YieldExpr>) visit(node.operand);
    else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, LogicalExpr> || std::is_same_v<T, SequenceExpr>) {
      visit(node.left); visit(node.right);
    } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
      visit(node.condition); visit(node.consequent); visit(node.alternate);
    } else if constexpr (std::is_same_v<T, AssignmentExpr>) {
      if (const auto* member = std::get_if<MemberTarget>(&node.target)) { visit(member->base); key(member->key); }
      visit(node.value);
    } else if constexpr (std::is_same_v<T, FieldDefinitionExpr>) { visit(node.receiver); visit(node.value); }
    else if constexpr (std::is_same_v<T, CallExpr>) { visit(node.callee); if (node.array_arguments) visit(*node.array_arguments); else arguments(node.arguments); }
    else if constexpr (std::is_same_v<T, ConstructExpr>) {
      visit(node.constructor);
      if (const auto* range = std::get_if<ArgumentRange>(&node.arguments)) arguments(*range);
      else visit(std::get<ArrayArguments>(node.arguments).array);
    } else if constexpr (std::is_same_v<T, SuperCallExpr>) {
      if (const auto* range = std::get_if<ArgumentRange>(&node.arguments)) arguments(*range);
      else visit(std::get<SpreadArguments>(node.arguments).iterable);
    }
    else if constexpr (std::is_same_v<T, ArgumentSnapshotExpr>) arguments(node.slots);
    else if constexpr (std::is_same_v<T, ArrayExpr>) arguments(node.elements);
    else if constexpr (std::is_same_v<T, ArrayElement>) {
      if (const auto* value = std::get_if<ArrayValueElement>(&node.element)) visit(value->value);
      else if (const auto* spread = std::get_if<ArraySpreadElement>(&node.element)) visit(spread->iterable);
    }
    else if constexpr (std::is_same_v<T, ObjectExpr>) {
      for (std::uint32_t i = 0; i < node.properties.count; ++i) visit(tree.properties[node.properties.first + i].value);
    } else if constexpr (std::is_same_v<T, ClassExpr>) { if (node.heritage) visit(*node.heritage); }
    else {
      static_assert(std::is_same_v<T, TreeLiteral> || std::is_same_v<T, SymbolExpr> ||
          std::is_same_v<T, GlobalNameExpr> || std::is_same_v<T, RealmObjectExpr> ||
          std::is_same_v<T, SymbolConstructorExpr> || std::is_same_v<T, ThisExpr> ||
          std::is_same_v<T, UpdateExpr> || std::is_same_v<T, EmptyArrayExpr> ||
          std::is_same_v<T, EmptyObjectExpr> || std::is_same_v<T, FunctionExpr>);
    }
  }, expression);
}
}
#endif
