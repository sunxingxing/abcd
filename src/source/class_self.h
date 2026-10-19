#ifndef ABCD_SOURCE_CLASS_SELF_H_
#define ABCD_SOURCE_CLASS_SELF_H_

#include "class_initializers.h"
#include "cell_plan.h"

namespace abcd::source {
struct ClassSelfStore {
  analysis::OpId creation;
  analysis::OpId store;
  analysis::OpId initializer;
  CellId cell;
  analysis::OpId allocation;
};
struct ClassSelfStores {
  std::vector<ClassSelfStore> stores;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ClassSelfError : std::uint8_t {
  kInput,
  kCells,
  kFlow,
  kResourceLimit,
  kWorkLimit,
  kRecoveryGate
};
// Discover class-result initialization of a fresh, otherwise unwritten local
// cell before its static initializer, with absent heritage, no intervening
// observer. Common SSA/effect identity and must-reach prove the private interval;
// all cell reads must follow the call on every path. This does not authorize moving the store:
// descendant capture ownership, name shadowing and native class scope must be
// proved by the eventual source owner. Never infer self from names or slot zero.
[[nodiscard]] std::variant<ClassSelfStores, ClassSelfError> FindClassSelfStores(
    const analysis::ValueProgram&, const analysis::ValueEnvironments&, const CellPlan&,
    std::span<const ClassInitializerCall>, analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
