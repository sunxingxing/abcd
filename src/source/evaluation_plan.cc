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
    bool has_phi = false;
    for (const auto& value : graph.values) {
      step();
      has_phi |= value.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::PhiDefinition>(value.origin);
    }
    EvaluationPlan plan{graph.revision, graph.cfg_revision, {}};
    admit(graph.ops.size(), sizeof(std::uint8_t)); step(graph.ops.size());
    std::vector<std::uint8_t> pinned(graph.ops.size());
    admit(pinned.capacity() - graph.ops.size(), sizeof(std::uint8_t));
    bool has_definitions = false;
    bool has_cells = false;
    for (const auto& op : graph.ops) {
      step(); has_definitions |= op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueOwnWrite>(op.operation);
      has_cells |= op.life == analysis::ValueLife::kLive && NeedsCellEvaluation(op.operation);
    }
    if (has_definitions) {
      auto built = BuildLiteralPlan(graph, {bytes, work});
      const auto* error = std::get_if<LiteralError>(&built);
      if (error) {
        if (*error == LiteralError::kResourceLimit) return Error::kResourceLimit;
        if (*error == LiteralError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      plan.literals = std::get<LiteralPlan>(std::move(built));
      admit(plan.literals->allocated_bytes, 1); step(plan.literals->work);
      for (const auto& object : plan.literals->objects) {
        step(); pinned[object.allocation.value] = 1;
      }
      for (const auto id : plan.literals->definitions) {
        step();
        const auto& rhs = graph.uses[graph.ops[id.value].first_use.value + 1].operand;
        if (const auto* value = std::get_if<analysis::ValueId>(&rhs))
          if (const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin))
            pinned[result->op.value] = 1;
      }
    }
    if (has_cells) {
      auto built = BuildCellEvaluation(graph, {bytes, work});
      if (const auto* error = std::get_if<CellEvaluationError>(&built)) {
        if (*error == CellEvaluationError::kResourceLimit) return Error::kResourceLimit;
        if (*error == CellEvaluationError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      plan.cells = std::get<CellEvaluation>(std::move(built));
      admit(plan.cells->allocated_bytes, 1); step(plan.cells->work);
    }
    if (has_phi) {
      auto copies = BuildPhiCopies(graph, {bytes, work});
      auto* phis = std::get_if<PhiCopies>(&copies);
      if (!phis) {
        const auto error = std::get<PhiCopyError>(copies);
        if (error == PhiCopyError::kResourceLimit) return Error::kResourceLimit;
        if (error == PhiCopyError::kWorkLimit) return Error::kWorkLimit;
        return Error::kLivePhi;
      }
      admit(phis->allocated_bytes, 1); step(phis->work);
      plan.phis = std::move(*phis);
    }
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
        if (IsEnvironmentState(op.operation)) {
          Require(plan.cells.has_value(), Error::kUnsupportedOperation);
          plan.operations[index].kind = EvaluationKind::kEnvironment;
          continue;
        }
        const auto shape = DescribeEvaluation(op.operation);
        Require(shape.has_value(), Error::kUnsupportedOperation);
        Require(op.use_count == shape->Count() && shape->expression == op.result.has_value(),
                Error::kInvalidGraph);
        auto& placement = plan.operations[index];
        if (std::holds_alternative<analysis::ValueOwnWrite>(op.operation)) {
          Require(plan.literals.has_value(), Error::kUnsupportedOperation);
          placement.kind = EvaluationKind::kInitializer;
          continue;
        }
        placement.kind = EvaluationKind::kRoot;
        if (op.result && graph.values[op.result->value].first_use) {
          const auto use = *graph.values[op.result->value].first_use;
          const auto& occurrence = graph.uses[use.value];
          if (occurrence.next) placement.temporary = TemporaryReason::kMultipleUses;
          else {
            const auto* owner = std::get_if<analysis::OpId>(&occurrence.owner);
            placement.temporary = !owner || graph.ops[owner->value].block != op.block
                ? TemporaryReason::kCrossRegionValue
                : PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand)
                    ? TemporaryReason::kPredicateSnapshot : TemporaryReason::kEffectSnapshot;
          }
        }
        auto expected = serial;
        std::uint32_t effects = 0;
        // Select a suffix of already-executed effects in reverse declared input
        // order. Call reference legality is a later proof. A rejected child
        // stays materialized at its original point.
        // Subtree intervals include inlined getters, so a pure parent cannot
        // carry a getter across an intervening write/call by accident.
        for (std::uint32_t ordinal = op.use_count; ordinal != 0; --ordinal) {
          step();
          const analysis::UseId use{op.first_use.value + ordinal - 1};
          const auto& occurrence = graph.uses[use.value];
          Require(occurrence.purpose == analysis::ReadPurpose::kOperand, Error::kProtocolOperand);
          Require(occurrence.role == shape->Role(ordinal - 1), Error::kInvalidGraph);
          const auto* value = std::get_if<analysis::ValueId>(&occurrence.operand);
          if (!value) continue;
          const auto& definition = graph.values[value->value];
          const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
          if (!result || definition.first_use != use || occurrence.next) continue;
          if (pinned[result->op.value]) continue;
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
    plan.allocated_bytes = plan.operations.capacity() * sizeof(OpEvaluation) +
        (plan.phis ? plan.phis->allocated_bytes : 0) +
        (plan.literals ? plan.literals->allocated_bytes : 0);
    if (plan.cells) plan.allocated_bytes += plan.cells->allocated_bytes;
    plan.work = budget.work - work;
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
