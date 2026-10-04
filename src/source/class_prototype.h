#ifndef ABCD_SOURCE_CLASS_PROTOTYPE_H_
#define ABCD_SOURCE_CLASS_PROTOTYPE_H_
#include "abcd/analysis/value_ir.h"
#include "expression_constants.h"
namespace abcd::source {
// Conditional key obligation: named access uses its own NameId; computed
// access uses the actual SSA String producer's TextId. Never infer a key from
// a value domain, compiler layout, class name or printed source.
struct ClassPrototypeRead {
  analysis::OpId read;
  analysis::OpId creation;
  std::optional<analysis::OpId> key;
  bool operator==(const ClassPrototypeRead&) const = default;
};
struct ClassPrototypeReads {
  std::vector<ClassPrototypeRead> reads;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ClassPrototypeError : std::uint8_t { kFlow, kResourceLimit, kWorkLimit };
// Fresh availability and use verification precede the common copy/phi SCC
// identity analysis. Candidates may escape or span any CFG; their fixed own
// property is immutable. The source boundary must still resolve "prototype".
[[nodiscard]] std::variant<ClassPrototypeReads, ClassPrototypeError> FindClassPrototypeReads(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
[[nodiscard]] bool HasClassPrototypeKey(const ClassPrototypeRead&, ExpressionConstants,
    std::span<const std::u16string> names, std::span<const std::u16string> texts);
}
#endif
