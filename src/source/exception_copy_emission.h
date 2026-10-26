#ifndef ABCD_SOURCE_EXCEPTION_COPY_EMISSION_H_
#define ABCD_SOURCE_EXCEPTION_COPY_EMISSION_H_

#include "exception_copies.h"
#include <type_traits>

namespace abcd::source {
struct EvaluationPlan;
class TreeCheckBudget;
enum class ExceptionStageDomain : std::uint8_t { kOriginal, kUniform };
inline bool SameStageOperand(const analysis::ValueOperand& a, const analysis::ValueOperand& b) {
  return std::visit([](const auto& x, const auto& y) {
    if constexpr (!std::is_same_v<std::decay_t<decltype(x)>, std::decay_t<decltype(y)>>) return false;
    else if constexpr (std::is_same_v<std::decay_t<decltype(x)>, analysis::SpecialValue>) return x == y;
    else return x.value == y.value;
  }, a, b);
}
struct StageAssignment {
  CatchSlotId slot;
  analysis::ValueOperand source;
  bool operator==(const StageAssignment& other) const {
    return slot == other.slot && SameStageOperand(source, other.source);
  }
};
struct StageGroup {
  analysis::OpId root, entry;
  analysis::WorkBlockId handler;
  std::uint32_t bundle, first, count;
  bool operator==(const StageGroup&) const = default;
};
struct StageCoverage {
  std::uint32_t bundle, copy, assignment, group;
  analysis::OpId operation;
  analysis::EdgeId edge;
  bool operator==(const StageCoverage&) const = default;
};
// Original exceptional copies remain authoritative. Physical assignments have
// one owner; coverage is a relation, never duplicated syntax or a removed exit.
struct ExceptionCopyEmission {
  std::vector<StageGroup> groups;
  std::vector<StageAssignment> assignments;
  std::vector<StageCoverage> coverage;
  std::vector<std::optional<std::uint32_t>> bundles;
  std::vector<std::optional<std::uint32_t>> entries;
  bool operator==(const ExceptionCopyEmission&) const = default;
  [[nodiscard]] std::size_t AllocatedBytes() const;
};
// Exact interned snapshots, indexed by OpId. Absence is not an empty snapshot.
// Each original copy is inspected O(log K) times, not once per ancestor.
std::vector<std::optional<std::uint32_t>> IndexExceptionSnapshots(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
ExceptionCopyEmission BuildExceptionCopyEmission(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
void VerifyExceptionCopyEmission(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
