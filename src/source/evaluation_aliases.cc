#include "evaluation_aliases.h"

#include <new>
#include <stdexcept>
#include "../analysis/value_edits.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class AliasQuota {
 public:
  explicit AliasQuota(SemanticBudget limit) : initial_(limit), left_(limit) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= left_.work, Error::kWorkLimit); left_.work -= count;
  }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Require(count <= left_.bytes / sizeof(T), Error::kResourceLimit);
    left_.bytes -= count * sizeof(T); Step(count); values.resize(count);
    const auto extra = values.capacity() - count;
    Require(extra <= left_.bytes / sizeof(T), Error::kResourceLimit); left_.bytes -= extra * sizeof(T);
  }
  EvaluationAliasCost Cost() const { return {initial_.bytes - left_.bytes, initial_.work - left_.work}; }
 private:
  SemanticBudget initial_, left_;
};
std::optional<ValueId> AliasInput(const ValueProgram& graph, const ValueOp& op) {
  if (op.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(op.operation) ||
      !CanErasePureOp(op) || !op.result || op.use_count != 1) return {};
  const auto& use = graph.uses[op.first_use.value];
  const auto* source = std::get_if<ValueId>(&use.operand);
  if (!source || use.role != OperandRole::kInput || use.purpose != ReadPurpose::kOperand) return {};
  const auto& value = graph.values[source->value];
  // Phi targets are mutable during parallel edge copies; catch symbols have
  // handler-local scope. Preserve their first materializing copy. Later copies
  // can refer to that immutable SSA result without changing these contracts.
  if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
    if (!std::holds_alternative<ParameterEntry>(*entry) && !std::holds_alternative<ReceiverEntry>(*entry)) return {};
  } else if (!std::holds_alternative<OpResult>(value.origin)) return {};
  // A copy carrying stronger certified facts must not be replaced by a root
  // with a broader domain. Do not manufacture a guard/refinement here.
  const auto admitted = graph.values[op.result->value].domain;
  if (value.domain.IsEmpty() || value.domain.Join(admitted) != admitted) return {};
  return *source;
}
enum class AliasVisit : std::uint8_t { kUnseen, kActive, kResolved };
}  // namespace

std::variant<EvaluationAliasCost, EvaluationError> BuildEvaluationAliases(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.flow == EvaluationFlow::kPhased && plan.operations.size() == graph.ops.size() &&
        plan.aliases.empty(), Error::kOwnership);
    AliasQuota quota(budget);
    quota.Array(plan.aliases, graph.values.size());
    std::vector<AliasVisit> visits;
    std::vector<ValueId> pending;
    quota.Array(visits, graph.values.size()); quota.Array(pending, graph.values.size());
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Step(); plan.aliases[i] = {static_cast<std::uint32_t>(i)};
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step();
      if (plan.operations[i].kind == EvaluationKind::kElidedPure) continue;
      const auto& op = graph.ops[i];
      if (const auto source = AliasInput(graph, op)) {
        Require(*source != *op.result, Error::kOwnership);
        plan.aliases[op.result->value] = *source;
        plan.operations[i].kind = EvaluationKind::kAlias;
      }
    }
    // Functional-graph compression, as in value_simplify.cc. Each unresolved
    // vertex is visited once; no per-use/per-call walk down a copy chain.
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Step();
      if (visits[i] == AliasVisit::kResolved) continue;
      std::size_t count = 0;
      ValueId cursor{static_cast<std::uint32_t>(i)};
      while (visits[cursor.value] == AliasVisit::kUnseen && plan.aliases[cursor.value] != cursor) {
        quota.Step(); visits[cursor.value] = AliasVisit::kActive;
        pending[count++] = cursor; cursor = plan.aliases[cursor.value];
      }
      Require(visits[cursor.value] != AliasVisit::kActive, Error::kOwnership);
      const auto root = plan.aliases[cursor.value];
      visits[cursor.value] = AliasVisit::kResolved;
      for (std::size_t j = 0; j < count; ++j) {
        quota.Step(); plan.aliases[pending[j].value] = root;
        visits[pending[j].value] = AliasVisit::kResolved;
      }
    }
    return quota.Cost();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<std::uint64_t, EvaluationError> VerifyEvaluationAliases(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  try {
    if (plan.flow == EvaluationFlow::kNormal) {
      Require(plan.aliases.empty(), Error::kOwnership);
      return std::uint64_t{0};
    }
    Require(plan.aliases.size() == graph.values.size(), Error::kOwnership);
    AliasQuota quota(budget);
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Step();
      const auto root = plan.aliases[i];
      Require(root.value < graph.values.size() && plan.aliases[root.value] == root, Error::kOwnership);
      const auto* result = std::get_if<OpResult>(&graph.values[i].origin);
      if (root.value == i) {
        Require(!result || plan.operations[result->op.value].kind != EvaluationKind::kAlias, Error::kOwnership);
        continue;
      }
      Require(result && plan.operations[result->op.value].kind == EvaluationKind::kAlias, Error::kOwnership);
      const auto& op = graph.ops[result->op.value];
      const auto source = AliasInput(graph, op);
      Require(source && op.result == ValueId{static_cast<std::uint32_t>(i)} &&
          plan.aliases[source->value] == root, Error::kOwnership);
    }
    return quota.Cost().work;
  } catch (Error error) { return error; }
}
}  // namespace abcd::source
