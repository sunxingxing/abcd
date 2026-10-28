#ifndef ABCD_SOURCE_PHASE_CORRIDOR_H_
#define ABCD_SOURCE_PHASE_CORRIDOR_H_

#include "phase_evaluation.h"

#include <optional>

namespace abcd::source {
class PhaseBudget {
 public:
  explicit PhaseBudget(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::size_t count = 1);
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    if (count > remaining_.bytes / sizeof(T)) throw EvaluationError::kResourceLimit;
    Step(count); values.resize(count);
    if (values.capacity() > remaining_.bytes / sizeof(T)) throw EvaluationError::kResourceLimit;
    remaining_.bytes -= values.capacity() * sizeof(T);
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  void Charge(std::size_t bytes, std::uint64_t work) {
    if (bytes > remaining_.bytes) throw EvaluationError::kResourceLimit;
    Step(work); remaining_.bytes -= bytes;
  }
  PhaseEvaluationCost Cost() const {
    return {initial_.bytes - remaining_.bytes, initial_.work - remaining_.work};
  }
 private:
  analysis::SemanticBudget initial_, remaining_;
};
struct PhasePosition {
  std::optional<analysis::WorkBlockId> head;
  std::size_t order = 0, effects = 0, copies = 0;
};
struct PhaseUse { std::optional<analysis::UseId> first; bool multiple = false; };
struct PhaseEffectInterval { std::size_t count = 0, end = 0; };
enum class CheckedReadFusion : std::uint8_t { kStandalone, kCompose };
enum class PhaseLiteralIntervals : std::uint8_t { kExclude, kInclude };
// O(B+E+I+V+U+P+S) indexing. Slot comparisons are charged to uniquely consumed
// producers; never perform a fresh graph walk per expression or operand.
class PhaseCorridors {
 public:
  PhaseCorridors(const analysis::ValueProgram& graph, const EvaluationPlan& plan, PhaseBudget& budget,
      PhaseLiteralIntervals literals = PhaseLiteralIntervals::kInclude,
      CheckedReadFusion checked = CheckedReadFusion::kStandalone);
  bool CanFuse(analysis::OpId producer, analysis::OpId consumer, analysis::UseId use) const;
  // The coordinate-free part of CanFuse: nullopt rejects; true means an
  // invariant pure value needing no corridor; false needs an ordering proof
  // (corridor coordinates here, or the composition order proof).
  [[nodiscard]] std::optional<bool> FuseSemantics(analysis::OpId producer, analysis::OpId consumer,
      analysis::UseId use) const;
  bool SameCatch(analysis::OpId producer, analysis::OpId root);
  bool HasCatchCopies(analysis::OpId operation) const;
  // The op's single result use is owned by a phi (a selection-arm value).
  bool PhiRoot(analysis::OpId operation) const;
  bool NeedsLocalStage(analysis::OpId operation) const {
    return plan_.exception_stage_domain == ExceptionStageDomain::kOriginal && HasCatchCopies(operation);
  }
  // The value's only canonical (non-receiver, non-identity) occurrence.
  [[nodiscard]] bool SoleUse(analysis::ValueId value, analysis::UseId use) const {
    const auto& occurrence = uses_[value.value];
    return !occurrence.multiple && occurrence.first == use;
  }
  std::size_t EffectsBefore(analysis::OpId op) const;
  std::size_t EffectCount(analysis::OpId op) const;
  PhaseEffectInterval Interval(analysis::OpId op) const;
  static bool Aggregate(const analysis::ValueOperation& operation);
  static bool Expression(const analysis::ValueOperation& operation);
  // A statement or suspension root can own an eager operand expression.
  // This does not authorize moving the root itself; reference/conversion and
  // suspension boundaries remain DescribeEvaluation obligations.
  static bool ConsumesOperands(const analysis::ValueOperation& operation);
  static bool Event(const analysis::ValueOp& op, const OpEvaluation& placement);
 private:
  const analysis::ValueProgram& graph_;
  const EvaluationPlan& plan_;
  PhaseBudget& budget_;
  CheckedReadFusion checked_;
  std::vector<PhasePosition> positions_;
  std::vector<PhaseUse> uses_;
  std::vector<std::optional<std::size_t>> exits_, throws_, staged_;
  std::vector<std::optional<std::uint32_t>> snapshots_;
  std::vector<std::optional<analysis::OpId>> module_peers_;
  std::vector<std::optional<analysis::OpId>> literal_ends_;
};
}  // namespace abcd::source
#endif
