#ifndef ABCD_SOURCE_EXPRESSION_CONSTANTS_H_
#define ABCD_SOURCE_EXPRESSION_CONSTANTS_H_

#include <span>

#include "abcd/source/source_tree.h"

namespace abcd::source {
struct FileFunction;
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
// Language-defined keys for input-backed intrinsic records. Resolve these
// once per method, like buffered keys; no pool ID reaches executable syntax.
struct IterResultConstant {
  // JSIterator::CreateIterResultObject, ECMA CreateIterResultObject steps 3/4.
  static constexpr std::uint32_t kProperties = 2;
  NameId value;
  NameId done;
};
// Record binding adapter supplies the same metadata slot and native symbol.
// Initialization is a declaration at this exact store, never an eager `let`.
struct ModuleStoreConstant {
  analysis::ModuleSlot slot;
  ModuleSymbol target;
  bool initialize = false;
};
using ExpressionConstant = std::variant<std::monostate, NameId, TextId, AggregateConstant, IterResultConstant, ModuleStoreConstant,
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
  // Only retained during construction; semantic verification reproves this
  // exact owner/input/work pair before any immutable family is published.
  const FileFunction* implicit_constructor = nullptr;
  // Exact original invocation and work graph; the common derived-state solver
  // independently proves constructor identity through aliases, cells and phis.
  const FileFunction* derived_constructor = nullptr;
};
}  // namespace abcd::source
#endif
