#ifndef ABCD_SOURCE_EXPRESSION_CONSTANTS_H_
#define ABCD_SOURCE_EXPRESSION_CONSTANTS_H_

#include <span>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Constants are resolved by the ABC adapter before syntax construction. Pool
// indices never become executable tree operands. Entries are indexed by OpId.
// Sealed signature projection supplies pool-backed literals at removed sites.
struct SignatureString { analysis::OpId operation; abc::PoolIndex string; };
struct AggregateConstant {
  std::uint32_t first;
  std::uint32_t count;
  AggregateKind kind = AggregateKind::kObject;
};
struct ConstantProperty { NameId key; TreeLiteral value; };
// Record binding adapter supplies the same metadata slot and native symbol.
// Initialization is a declaration at this exact store, never an eager `let`.
struct ModuleStoreConstant {
  analysis::ModuleSlot slot;
  ModuleSymbol target;
  bool initialize = false;
};
using ExpressionConstant = std::variant<std::monostate, NameId, TextId, AggregateConstant, ModuleStoreConstant,
    std::shared_ptr<const ClassTree>>;
struct ExpressionConstants {
  std::span<const ExpressionConstant> operations;
  std::size_t name_count = 0;
  std::size_t text_count = 0;
  std::optional<TextId> number_type_text;
  std::span<const ConstantProperty> properties;
  // Same-owner ProjectDerivedConstructor result, revalidated after defaults.
  // Sorted original guard IDs; these become anchors, never JS this reads.
  std::span<const analysis::OpId> receiver_guards;
};
}  // namespace abcd::source
#endif
