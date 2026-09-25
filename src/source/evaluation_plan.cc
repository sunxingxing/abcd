#include "evaluation_plan.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct EffectInterval { std::uint32_t count = 0; std::uint32_t end = 0; };
}  // namespace

std::variant<EvaluationPlan, EvaluationError> BuildEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  try {
    auto work = budget.work;
    auto bytes = budget.bytes;
    const auto step = [&](std::size_t count = 1) {
      Require(count <= work, Error::kWorkLimit);
      work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= bytes / width, Error::kResourceLimit);
      bytes -= count * width;
    };
    Require(graph.ops.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    admit(graph.uses.size(), sizeof(std::uint8_t));
    step(graph.values.size()); step(graph.ops.size());
    step(graph.uses.size()); step(graph.uses.size()); step(graph.uses.size());
    if (const auto error = analysis::VerifyValueUses(graph, graph.uses.size()))
      return *error == analysis::ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kInvalidGraph;
    for (const auto& value : graph.values) {
      step();
      Require(value.life != analysis::ValueLife::kLive ||
          !std::holds_alternative<analysis::PhiDefinition>(value.origin), Error::kLivePhi);
    }
    EvaluationPlan plan{graph.revision, graph.cfg_revision, {}};
    admit(graph.ops.size(), sizeof(OpEvaluation));
    admit(graph.ops.size(), sizeof(EffectInterval));
    step(graph.ops.size()); step(graph.ops.size());
    plan.operations.resize(graph.ops.size());
    std::vector<EffectInterval> groups(graph.ops.size());
    Require(plan.operations.capacity() <= graph.ops.size() && groups.capacity() <= graph.ops.size(),
            Error::kResourceLimit);
    std::size_t next_op = 0;
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step();
      const auto& block = graph.blocks[b];
      Require(block.first_op.value == next_op && block.op_count <= graph.ops.size() - next_op,
              Error::kInvalidGraph);
      std::uint32_t serial = 0;
      for (std::uint32_t order = 0; order < block.op_count; ++order) {
        step();
        const auto index = next_op + order;
        const auto& op = graph.ops[index];
        Require(op.block.value == b && op.order == order, Error::kInvalidGraph);
        if (op.life != analysis::ValueLife::kLive) continue;
        const auto shape = DescribeEvaluation(op.operation);
        Require(shape.has_value(), Error::kUnsupportedOperation);
        Require(op.use_count == shape->roles.size() && shape->expression == op.result.has_value(),
                Error::kInvalidGraph);
        auto& placement = plan.operations[index];
        placement.kind = EvaluationKind::kRoot;
        if (op.result && graph.values[op.result->value].first_use) {
          const auto use = *graph.values[op.result->value].first_use;
          const auto& occurrence = graph.uses[use.value];
          if (occurrence.next) placement.temporary = TemporaryReason::kMultipleUses;
          else {
            const auto* owner = std::get_if<analysis::OpId>(&occurrence.owner);
            Require(owner != nullptr, Error::kLivePhi);
            placement.temporary = graph.ops[owner->value].block != op.block
                ? TemporaryReason::kCrossRegionValue
                : PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand)
                    ? TemporaryReason::kPredicateSnapshot : TemporaryReason::kEffectSnapshot;
          }
        }
        auto expected = serial;
        std::uint32_t effects = 0;
        // Select a suffix of already-executed effects in reverse JS operand
        // order. A rejected child stays materialized at its original point.
        // Subtree intervals include inlined getters, so a pure parent cannot
        // carry a getter across an intervening write/call by accident.
        for (std::uint32_t ordinal = op.use_count; ordinal != 0; --ordinal) {
          step();
          const analysis::UseId use{op.first_use.value + ordinal - 1};
          const auto& occurrence = graph.uses[use.value];
          Require(occurrence.purpose == analysis::ReadPurpose::kOperand, Error::kProtocolOperand);
          Require(occurrence.role == shape->roles[ordinal - 1], Error::kInvalidGraph);
          const auto* value = std::get_if<analysis::ValueId>(&occurrence.operand);
          if (!value) continue;
          const auto& definition = graph.values[value->value];
          const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
          if (!result || definition.first_use != use || occurrence.next) continue;
          if (PredicateSnapshot(graph, op, occurrence.operand)) continue;
          const auto& producer = graph.ops[result->op.value];
          if (producer.block != op.block) continue;
          Require(producer.order < order, Error::kUnavailableValue);
          const auto interval = groups[result->op.value];
          if (interval.count != 0 && interval.end != expected) continue;
          Require(interval.count <= expected, Error::kInvalidGraph);
          expected -= interval.count;
          effects += interval.count;
          auto& child = plan.operations[result->op.value];
          Require(child.kind == EvaluationKind::kRoot, Error::kOwnership);
          child = {EvaluationKind::kInline, use, {}};
        }
        if (OrderedEvaluation(op)) { ++serial; ++effects; }
        groups[index] = {effects, effects ? serial : 0};
      }
      next_op += block.op_count;
    }
    Require(next_op == graph.ops.size(), Error::kInvalidGraph);
    const auto checked = VerifyEvaluationPlan(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationError>(&checked)) return *error;
    step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = plan.operations.capacity() * sizeof(OpEvaluation);
    plan.work = budget.work - work;
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
