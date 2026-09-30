#ifndef ABCD_ANALYSIS_VALUE_HERITAGE_H_
#define ABCD_ANALYSIS_VALUE_HERITAGE_H_
#include "abcd/analysis/class_heritage.h"
#include "abcd/analysis/value_ir.h"
namespace abcd::analysis {
struct ValueClassHeritage {
  OpId operation;
  UseId input;
  HeritageKind kind;
};
struct ValueHeritages {
  IrRevision revision;
  std::vector<ValueClassHeritage> classes;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ValueHeritageError : std::uint8_t { kInput, kDomain, kResourceLimit, kWorkLimit };
// Verified unchanged value IR. Resolve all class heritage inputs in O(O+V),
// caching pure-copy chains once. Hole is absent extends; null is derived.
// Unknown language values retain their actual input and possible runtime throw.
// This does not authorize deleting protocol uses or emitting unchecked bodies.
[[nodiscard]] std::variant<ValueHeritages, ValueHeritageError> AnalyzeValueHeritage(
    const ValueProgram&, SemanticBudget = {});
}
#endif
