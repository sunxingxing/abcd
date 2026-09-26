#include "evaluation_plan.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct EvaluationFrame { analysis::OpId op; std::uint32_t next_operand = 0; };
enum class EvaluationVisit : std::uint8_t { kUnseen, kActive, kDone };
}  // namespace

std::variant<std::uint64_t, EvaluationError> VerifyEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.operations.size() == graph.ops.size(), Error::kInvalidGraph);
    auto work = budget.work;
    const auto step = [&](std::size_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    auto bytes = budget.bytes;
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= bytes / width, Error::kResourceLimit); bytes -= count * width;
    };
    admit(graph.uses.size(), sizeof(std::uint8_t));
    step(graph.values.size()); step(graph.ops.size());
    step(graph.uses.size()); step(graph.uses.size()); step(graph.uses.size());
    if (const auto error = analysis::VerifyValueUses(graph, graph.uses.size()))
      return *error == analysis::ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kInvalidGraph;
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
    Require(has_definitions == plan.literals.has_value(), Error::kOwnership);
    Require(has_cells == plan.cells.has_value(), Error::kOwnership);
    if (plan.cells) {
      const auto checked = VerifyCellEvaluation(graph, *plan.cells, {bytes, work});
      if (const auto* error = std::get_if<CellEvaluationError>(&checked)) {
        if (*error == CellEvaluationError::kResourceLimit) return Error::kResourceLimit;
        if (*error == CellEvaluationError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      step(std::get<std::uint64_t>(checked));
    }
    if (plan.literals) {
      const auto checked = VerifyLiteralPlan(graph, *plan.literals, {bytes, work});
      if (const auto* error = std::get_if<LiteralError>(&checked)) {
        if (*error == LiteralError::kResourceLimit) return Error::kResourceLimit;
        if (*error == LiteralError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      step(std::get<std::uint64_t>(checked));
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
    bool has_phi = false;
    for (const auto& value : graph.values) {
      step();
      has_phi |= value.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::PhiDefinition>(value.origin);
    }
    Require(has_phi == plan.phis.has_value(), Error::kLivePhi);
    if (plan.phis) {
      const auto checked = VerifyPhiCopies(graph, *plan.phis, {bytes, work});
      if (const auto* error = std::get_if<PhiCopyError>(&checked)) {
        if (*error == PhiCopyError::kResourceLimit) return Error::kResourceLimit;
        if (*error == PhiCopyError::kWorkLimit) return Error::kWorkLimit;
        return Error::kLivePhi;
      }
      step(std::get<std::uint64_t>(checked));
    }
    admit(graph.ops.size(), sizeof(EvaluationFrame));
    admit(graph.ops.size(), sizeof(EvaluationVisit));
    step(graph.ops.size()); step(graph.ops.size());
    std::vector<EvaluationFrame> stack(graph.ops.size());
    std::vector<EvaluationVisit> visits(graph.ops.size());
    Require(stack.capacity() <= graph.ops.size() && visits.capacity() <= graph.ops.size(), Error::kResourceLimit);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      const auto& op = graph.ops[i];
      const auto& placement = plan.operations[i];
      if (op.life != analysis::ValueLife::kLive) {
        Require(placement.kind == EvaluationKind::kRemoved && !placement.consumer && !placement.temporary,
                Error::kOwnership);
        continue;
      }
      if (IsEnvironmentState(op.operation)) {
        Require(plan.cells && placement.kind == EvaluationKind::kEnvironment &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
        continue;
      }
      const auto shape = DescribeEvaluation(op.operation);
      Require(shape.has_value(), Error::kUnsupportedOperation);
      Require(shape->Count() == op.use_count && shape->expression == op.result.has_value(), Error::kInvalidGraph);
      if (std::holds_alternative<analysis::ValueOwnWrite>(op.operation)) {
        // Fresh LiteralPlan proved these definitions unobservable until complete.
        // They execute in object syntax, not as ordinary ordered heap writes;
        // producer snapshots remain pinned before allocation, never duplicated.
        Require(plan.literals && placement.kind == EvaluationKind::kInitializer &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
      } else if (placement.kind == EvaluationKind::kInline) {
        Require(!pinned[i], Error::kOwnership);
        Require(op.result && placement.consumer && !placement.temporary, Error::kOwnership);
        const auto& value = graph.values[op.result->value];
        Require(value.first_use == placement.consumer && placement.consumer->value < graph.uses.size(), Error::kOwnership);
        const auto& use = graph.uses[placement.consumer->value];
        const auto* owner = std::get_if<analysis::OpId>(&use.owner);
        Require(!use.next && owner && graph.ops[owner->value].block == op.block &&
                graph.ops[owner->value].order > op.order, Error::kOwnership);
        Require(!PredicateSnapshot(graph, graph.ops[owner->value], use.operand), Error::kOwnership);
      } else {
        Require(placement.kind == EvaluationKind::kRoot && !placement.consumer, Error::kOwnership);
        const bool used = op.result && graph.values[op.result->value].first_use.has_value();
        Require(used == placement.temporary.has_value(), Error::kOwnership);
        if (used) {
          const auto& use = graph.uses[graph.values[op.result->value].first_use->value];
          const auto* owner = std::get_if<analysis::OpId>(&use.owner);
          const auto reason = use.next ? TemporaryReason::kMultipleUses
              : !owner || graph.ops[owner->value].block != op.block ? TemporaryReason::kCrossRegionValue
              : PredicateSnapshot(graph, graph.ops[owner->value], use.operand)
                  ? TemporaryReason::kPredicateSnapshot : TemporaryReason::kEffectSnapshot;
          Require(placement.temporary == reason, Error::kOwnership);
        }
      }
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        step();
        const auto& use = graph.uses[op.first_use.value + ordinal];
        Require(use.purpose == analysis::ReadPurpose::kOperand, Error::kProtocolOperand);
        Require(use.role == shape->Role(ordinal), Error::kInvalidGraph);
      }
    }
    std::size_t next_op = 0;
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step();
      const auto& block = graph.blocks[b];
      Require(block.first_op.value == next_op && block.op_count <= graph.ops.size() - next_op,
              Error::kInvalidGraph);
      const auto end = next_op + block.op_count;
      auto expected = next_op;
      const auto skip_pure = [&] {
        while (expected < end && (graph.ops[expected].life != analysis::ValueLife::kLive ||
                                  plan.operations[expected].kind == EvaluationKind::kInitializer ||
                                  plan.operations[expected].kind == EvaluationKind::kEnvironment ||
                                  !OrderedEvaluation(graph.ops[expected]))) { step(); ++expected; }
      };
      for (auto i = next_op; i < end; ++i) {
        step();
        Require(graph.ops[i].block.value == b && graph.ops[i].order == i - next_op, Error::kInvalidGraph);
        if (plan.operations[i].kind != EvaluationKind::kRoot) continue;
        Require(visits[i] == EvaluationVisit::kUnseen, Error::kOwnership);
        std::size_t depth = 1;
        stack[0] = {{static_cast<std::uint32_t>(i)}, 0};
        visits[i] = EvaluationVisit::kActive;
        while (depth != 0) {
          step();
          auto& frame = stack[depth - 1];
          const auto& op = graph.ops[frame.op.value];
          if (frame.next_operand < op.use_count) {
            const analysis::UseId use_id{op.first_use.value + frame.next_operand++};
            const auto* value = std::get_if<analysis::ValueId>(&graph.uses[use_id.value].operand);
            if (!value) continue;
            const auto& definition = graph.values[value->value];
            // The independently checked phi plan establishes incoming values.
            // A phi use reads its binding; it never expands an input subtree.
            const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
            if (!result) continue;
            const auto& producer = graph.ops[result->op.value];
            const auto& placement = plan.operations[result->op.value];
            if (placement.kind == EvaluationKind::kInline) {
              Require(placement.consumer == use_id && producer.block == op.block &&
                      visits[result->op.value] == EvaluationVisit::kUnseen, Error::kOwnership);
              Require(depth < stack.size(), Error::kResourceLimit);
              visits[result->op.value] = EvaluationVisit::kActive;
              stack[depth++] = {result->op, 0};
            } else {
              Require(placement.kind == EvaluationKind::kRoot, Error::kOwnership);
              Require(producer.block != op.block || visits[result->op.value] == EvaluationVisit::kDone,
                      Error::kUnavailableValue);
            }
          } else {
            if (OrderedEvaluation(op)) {
              skip_pure();
              Require(expected < end && expected == frame.op.value, Error::kOrder);
              ++expected;
            }
            visits[frame.op.value] = EvaluationVisit::kDone;
            --depth;
          }
        }
      }
      skip_pure();
      Require(expected == end, Error::kOrder);
      next_op = end;
    }
    Require(next_op == graph.ops.size(), Error::kInvalidGraph);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      Require(graph.ops[i].life != analysis::ValueLife::kLive || visits[i] == EvaluationVisit::kDone,
              Error::kOwnership);
    }
    return budget.work - work;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
