#include "closed_values.h"
#include "evaluation_plan.h"
#include "../analysis/register_value_rule.h"
#include "../analysis/value_phi.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
bool Immediate(const ValueOperand& operand) {
  if (const auto* number = std::get_if<Float64Bits>(&operand))
    return detail::FloatNumberKinds(*number).IsNumber();
  return !std::holds_alternative<ValueId>(operand);
}
}  // namespace
bool ClosedValues::Contains(const analysis::ValueProgram& graph,
    const analysis::ValueOperand& operand) const {
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  return value ? value->value < roots_.size() &&
      graph.values[value->value].life == analysis::ValueLife::kLive &&
      graph.values[value->value].domain.IsLanguage() && closed_[roots_[value->value].value] : Immediate(operand);
}
bool ClosedValues::Expression(analysis::OpId id) const {
  return id.value < expressions_.size() && expressions_[id.value];
}
std::variant<ClosedValues, ClosedValueError> ClosedValues::Build(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ClosedValueError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error) { if (!ok) throw error; };
    const auto step = [&](std::uint64_t count = 1) {
      require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      require(count <= budget.bytes/width, Error::kResourceLimit); budget.bytes -= count*width;
    };
    const auto array = [&]<class T>(std::vector<T>& items, std::size_t count) {
      admit(count, sizeof(T)); step(count); items.resize(count);
      admit(items.capacity()-count, sizeof(T));
    };
    auto indexed = AnalyzeValueIdentities(graph, budget);
    if (const auto* error = std::get_if<ValueSimplifyError>(&indexed))
      return *error == ValueSimplifyError::kResourceLimit ? Error::kResourceLimit :
          *error == ValueSimplifyError::kWorkLimit ? Error::kWorkLimit : Error::kInput;
    auto identities = std::get<ValueIdentities>(std::move(indexed));
    admit(identities.allocated_bytes, 1); step(identities.work);
    ClosedValues result; result.roots_ = std::move(identities.canonical);
    array(result.closed_, graph.values.size()); array(result.expressions_, graph.ops.size());
    std::vector<std::optional<UseId>> heads, next;
    std::vector<std::uint32_t> pending;
    std::vector<ValueId> ready;
    array(heads, graph.values.size()); array(next, graph.uses.size());
    array(pending, graph.ops.size()); array(ready, graph.values.size());
    std::size_t tail = 0;
    // Sparse monotone worklist: each canonical value becomes closed once and
    // each actual operand occurrence discharges once, including repeated inputs.
    // Copy/phi SCCs already have flat roots; do not add layout-dependent scans.
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      const auto shape = DescribeEvaluation(op.operation);
      if (op.life != ValueLife::kLive || !op.result || OrderedEvaluation(op) || !shape ||
          !shape->expression || shape->inputs != InputEvaluation::kEager ||
          shape->source == SourceEvaluation::kUnsupported || shape->Count() != op.use_count ||
          graph.values[op.result->value].life != ValueLife::kLive ||
          !graph.values[op.result->value].domain.IsLanguage()) continue;
      bool eligible = true;
      for (std::uint32_t u = 0; u < op.use_count; ++u) {
        step(); const auto& use = graph.uses[op.first_use.value+u];
        if (CheckEvaluationUse(graph, use, *shape, u)) eligible = false;
        if (const auto* input = std::get_if<ValueId>(&use.operand)) {
          if (graph.values[input->value].life != ValueLife::kLive ||
              !graph.values[input->value].domain.IsLanguage()) eligible = false;
        } else if (!Immediate(use.operand)) eligible = false;
      }
      if (!eligible) continue;
      result.expressions_[i] = 1;
      const auto root = result.roots_[op.result->value];
      if (root != *op.result) continue;
      for (std::uint32_t u = 0; u < op.use_count; ++u) {
        step(); const UseId id{op.first_use.value+u};
        if (const auto* input = std::get_if<ValueId>(&graph.uses[id.value].operand)) {
          const auto dependency = result.roots_[input->value];
          next[id.value] = heads[dependency.value]; heads[dependency.value] = id;
          ++pending[i];
        }
      }
      if (!pending[i]) { result.closed_[root.value] = 1; ready[tail++] = root; }
    }
    for (std::size_t front = 0; front < tail; ++front) {
      step(); auto use = heads[ready[front].value];
      while (use) {
        step(); const auto owner = std::get<OpId>(graph.uses[use->value].owner);
        require(pending[owner.value] != 0, Error::kInput);
        if (!--pending[owner.value]) {
          const auto root = *graph.ops[owner.value].result;
          result.closed_[root.value] = 1; ready[tail++] = root;
        }
        use = next[use->value];
      }
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); if (result.expressions_[i])
        result.expressions_[i] = result.Contains(graph, ValueOperand{*graph.ops[i].result});
    }
    result.bytes_ = initial.bytes-budget.bytes; result.work_ = initial.work-budget.work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
