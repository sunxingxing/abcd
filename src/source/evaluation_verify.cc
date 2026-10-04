#include "integer_nan.h"
#include "evaluation_plan.h"
#include "evaluation_flow.h"
#include "evaluation_aliases.h"
#include "module_evaluation.h"
#include "phase_evaluation.h"
#include "phase_corridor.h"
#include "logical_regions.h"
#include "logical_evaluation.h"
#include "tree_checks.h"
#include "../analysis/value_edits.h"

#include <new>
#include <bit>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct EvaluationFrame { analysis::OpId op; std::uint32_t next_operand = 0; };
enum class EvaluationVisit : std::uint8_t { kUnseen, kActive, kDone };
}  // namespace

std::variant<std::uint64_t, EvaluationError> VerifyEvaluationForFlow(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget, EvaluationFlow flow) {
  try {
    Require(plan.flow == flow, Error::kUnsupportedOperation);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.operations.size() == graph.ops.size(), Error::kInvalidGraph);
    Require(plan.logical_objects.empty() || plan.logical_objects.size() == graph.ops.size(), Error::kOwnership);
    Require(plan.arguments == graph.arguments, Error::kOwnership);
    Require(plan.closed_expressions.empty() || (flow == EvaluationFlow::kNormal &&
        plan.closed_expressions.size() == graph.ops.size()), Error::kOwnership);
    auto work = budget.work;
    const auto step = [&](std::size_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    auto bytes = budget.bytes;
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= bytes / width, Error::kResourceLimit); bytes -= count * width;
    };
    const auto availability = CheckEvaluationFlow(graph, flow, {bytes, work});
    if (const auto* error = std::get_if<Error>(&availability)) return *error;
    step(std::get<std::uint64_t>(availability));
    admit(graph.uses.size(), sizeof(std::uint8_t));
    step(graph.values.size()); step(graph.ops.size());
    step(graph.uses.size()); step(graph.uses.size()); step(graph.uses.size());
    if (const auto error = analysis::VerifyValueUses(graph, graph.uses.size()))
      return *error == analysis::ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kInvalidGraph;
    bool has_holes = false;
    for (const auto& op : graph.ops) { step(); has_holes |= std::holds_alternative<analysis::ValueHole>(op.operation); }
    Require(plan.holes.has_value() == has_holes, Error::kOwnership);
    if (plan.holes) {
      const auto analyzed = AnalyzeHoleProtocol(graph, {bytes, work});
      const auto* protocol = std::get_if<HoleProtocol>(&analyzed);
      if (!protocol) {
        switch (std::get<HoleProtocolError>(analyzed)) {
          case HoleProtocolError::kInput: throw Error::kInvalidGraph;
          case HoleProtocolError::kResourceLimit: throw Error::kResourceLimit;
          case HoleProtocolError::kWorkLimit: throw Error::kWorkLimit;
          case HoleProtocolError::kEscape:
          case HoleProtocolError::kUnguardedThrow: throw Error::kUnsupportedOperation;
        }
      }
      Require(protocol->comparisons == plan.holes->comparisons &&
          protocol->rethrows == plan.holes->rethrows, Error::kOwnership);
      admit(protocol->allocated_bytes, 1); step(protocol->work);
    }
    if (plan.phis) Require(plan.phis->domain == (has_holes && flow == EvaluationFlow::kPhased
        ? PhiDomain::kHoleTransport : PhiDomain::kLanguage), Error::kOwnership);
    Require(plan.receiver_guards.empty() || flow == EvaluationFlow::kNormal, Error::kOwnership);
    for (std::size_t i = 0; i < plan.receiver_guards.size(); ++i) {
      step(); const auto id = plan.receiver_guards[i];
      Require((i == 0 || plan.receiver_guards[i - 1] < id) && id.value < graph.ops.size(), Error::kOwnership);
      const auto& op = graph.ops[id.value];
      Require(op.life == analysis::ValueLife::kLive && !op.result && op.use_count == 1 &&
          std::holds_alternative<analysis::ValueSuperCheck>(op.operation), Error::kOwnership);
      Require(op.first_use.value < graph.uses.size(), Error::kInvalidGraph);
      const auto& use = graph.uses[op.first_use.value];
      Require(use.life == analysis::ValueLife::kLive && use.owner == decltype(use.owner){id} &&
          use.role == analysis::OperandRole::kGuardValue && use.purpose == analysis::ReadPurpose::kProtocol,
          Error::kOwnership);
    }
    const auto aliases = VerifyEvaluationAliases(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<Error>(&aliases)) return *error;
    step(std::get<std::uint64_t>(aliases));
    const auto normalized = VerifyIntegerNaNs(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<Error>(&normalized)) return *error;
    step(std::get<std::uint64_t>(normalized));
    auto indexed_modules = IndexModuleEvaluations(graph, plan.modules, {bytes, work});
    if (const auto* error = std::get_if<EvaluationError>(&indexed_modules)) return *error;
    auto module_index = std::get<ModuleEvaluationIndex>(std::move(indexed_modules));
    admit(module_index.allocated_bytes, 1); step(module_index.work);
    admit(graph.ops.size(), sizeof(std::uint8_t)); step(graph.ops.size());
    std::vector<std::uint8_t> pinned(graph.ops.size());
    admit(pinned.capacity() - graph.ops.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::uint8_t));
    step(graph.ops.size()); step(graph.ops.size());
    std::vector<std::uint8_t> literal_allocations(graph.ops.size());
    std::vector<EvaluationUses> visible;
    admit(literal_allocations.capacity() - graph.ops.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::size_t));
    std::vector<std::size_t> environment_epochs(graph.ops.size());
    admit(environment_epochs.capacity() - graph.ops.size(), sizeof(std::size_t));
    std::size_t environment_epoch = 0;
    auto projected = ProjectClassMembers(graph, plan.class_initializers, {bytes, work}, plan.class_definitions, plan.self_order ? &*plan.self_order : nullptr, plan.class_prototype_reads);
    if (const auto* error = std::get_if<ClassInitializerError>(&projected))
      return *error == ClassInitializerError::kResourceLimit ? Error::kResourceLimit :
          *error == ClassInitializerError::kWorkLimit ? Error::kWorkLimit : Error::kOwnership;
    const auto& projection = std::get<ClassProjection>(projected);
    admit(projection.allocated_bytes, 1); step(projection.work);
    Require(projection.owners == plan.class_projection.owners && projection.prototypes == plan.class_projection.prototypes,
        Error::kOwnership);
    const auto projected_op = [&](std::size_t i) { return !projection.owners.empty() && projection.owners[i].has_value(); };
    bool has_definitions = false;
    bool has_cells = plan.cells && (!plan.cells->cells.demands.empty() || !plan.cells->cells.entry_demands.empty());
    bool has_arguments = false;
    std::size_t next_function = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      const auto shape = op.life == analysis::ValueLife::kLive ? DescribeEvaluation(op.operation) : std::nullopt;
      has_arguments |= shape && shape->NeedsArgumentFrame();
      if (op.life == analysis::ValueLife::kLive && IsEnvironmentState(op.operation)) ++environment_epoch;
      environment_epochs[i] = environment_epoch;
      step(); has_definitions |= !projected_op(i) && op.life == analysis::ValueLife::kLive &&
          (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation));
      has_cells |= op.life == analysis::ValueLife::kLive && NeedsCellEvaluation(op.operation);
      if (op.life == analysis::ValueLife::kLive && !projected_op(i) && std::holds_alternative<analysis::ValueFunctionCreation>(op.operation)) {
        Require(next_function < plan.functions.size() && plan.functions[next_function++].creation.value == i, Error::kOwnership);
      }
    }
    Require(next_function == plan.functions.size(), Error::kOwnership);
    Require(has_arguments == graph.arguments.has_value(), Error::kOwnership);
    Require(has_definitions == plan.literals.has_value(), Error::kOwnership);
    Require(has_cells == plan.cells.has_value(), Error::kOwnership);
    if (plan.cells) {
      Require(plan.cells->initialization.closure_writes == (plan.functions.empty()
          ? plan.closure_writes : analysis::ClosureWrites::kLanguage), Error::kOwnership);
      const auto verify_cells = flow == EvaluationFlow::kPhased ? VerifyPhasedCellEvaluation : VerifyCellEvaluation;
      const auto checked = verify_cells(graph, *plan.cells, {bytes, work});
      if (const auto* error = std::get_if<CellEvaluationError>(&checked)) {
        if (*error == CellEvaluationError::kResourceLimit) return Error::kResourceLimit;
        if (*error == CellEvaluationError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      step(std::get<std::uint64_t>(checked));
    }
    for (const auto& function : plan.functions) {
      step();
      for (const auto cell : function.captures) {
        step(); Require(plan.cells && cell.value < plan.cells->cells.cells.size(), Error::kOwnership);
      }
    }
    if (plan.literals) {
      const auto checked = flow == EvaluationFlow::kPhased ?
          VerifyPhasedLiteralPlan(graph, *plan.literals, plan, {bytes, work}, module_index.peers) :
          VerifyLiteralPlan(graph, *plan.literals, {bytes, work}, module_index.peers, plan.class_projection.owners, plan.receiver_guards, plan.cells ? &*plan.cells : nullptr);
      if (const auto* error = std::get_if<LiteralError>(&checked)) {
        if (*error == LiteralError::kResourceLimit) return Error::kResourceLimit;
        if (*error == LiteralError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      step(std::get<std::uint64_t>(checked));
      for (const auto& object : plan.literals->objects) {
        step();
        if (object.form == LiteralForm::kStatements) continue;
        if (flow == EvaluationFlow::kNormal) literal_allocations[object.allocation.value] = 1;
        else pinned[object.allocation.value] = 1;
      }
      for (const auto id : plan.literals->definitions) {
        step();
        const auto rhs = ResolveEvaluationOperand(plan, graph.uses[graph.ops[id.value].first_use.value + 1].operand);
        if (const auto* value = std::get_if<analysis::ValueId>(&rhs))
          if (const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin))
            pinned[result->op.value] = 1;
      }
    }
    // Recompute actual observations after identity projection, including uses
    // transported through phi/copy aliases. Each original use is visited once.
    TreeCheckBudget use_budget({bytes,work});
    visible = IndexEvaluationUses(graph,plan,module_index,literal_allocations,projection.owners,
        flow == EvaluationFlow::kNormal && plan.cells ? &*plan.cells : nullptr,plan.receiver_guards,use_budget);
    admit(use_budget.Bytes(),1); step(use_budget.Work());

    bool has_phi = false;
    for (const auto& value : graph.values) {
      step();
      has_phi |= value.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::PhiDefinition>(value.origin);
    }
    Require(has_phi == plan.phis.has_value(), Error::kLivePhi);
    Require(plan.exceptions.has_value() == (flow == EvaluationFlow::kPhased && has_phi), Error::kLivePhi);
    if (plan.phis) {
      const auto checked = flow == EvaluationFlow::kNormal ? VerifyPhiCopies(graph, *plan.phis, {bytes, work})
          : VerifyPhasedPhiCopies(graph, *plan.phis, {bytes, work});
      if (const auto* error = std::get_if<PhiCopyError>(&checked)) {
        if (*error == PhiCopyError::kResourceLimit) return Error::kResourceLimit;
        if (*error == PhiCopyError::kWorkLimit) return Error::kWorkLimit;
        return Error::kLivePhi;
      }
      step(std::get<std::uint64_t>(checked));
    }
    if (plan.exceptions) {
      const auto checked = VerifyExceptionCopies(graph, *plan.phis, *plan.exceptions, {bytes, work});
      if (const auto* error = std::get_if<ExceptionCopyError>(&checked)) {
        if (*error == ExceptionCopyError::kResourceLimit) return Error::kResourceLimit;
        if (*error == ExceptionCopyError::kWorkLimit) return Error::kWorkLimit;
        return Error::kLivePhi;
      }
      step(std::get<std::uint64_t>(checked));
    }
    std::vector<std::optional<analysis::UseId>> phi_inputs;
    if (plan.phis) {
      TreeCheckBudget phi_budget({bytes, work});
      const auto literal_ends = plan.literals ? IndexPhiLiteralEnds(graph, *plan.literals, phi_budget) :
          std::vector<std::optional<analysis::OpId>>{};
      phi_inputs = IndexPhiExpressionInputs(graph, *plan.phis, module_index.peers, literal_ends, phi_budget);
      admit(phi_budget.Bytes(), 1); step(phi_budget.Work());
    }
    TreeCheckBudget logical_budget({bytes, work});
    const auto logical_inputs = FindLogicalLeftInputs(graph, plan, logical_budget);
    admit(logical_budget.Bytes(), 1); step(logical_budget.Work());
    TreeCheckBudget fusion_budget({bytes, work});
    VerifyLogicalValues(graph, plan, fusion_budget);
    admit(fusion_budget.Bytes(), 1); step(fusion_budget.Work());
    const auto module_copies = VerifyModuleCatchCopies(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<Error>(&module_copies)) return *error;
    step(std::get<std::uint64_t>(module_copies));
    const auto expressions = VerifyPhaseExpressions(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<Error>(&expressions)) return *error;
    const auto& phase_cost = std::get<PhaseEvaluationCost>(expressions);
    admit(phase_cost.bytes, 1); step(phase_cost.work);
    // Cross-block events are not erased: the independent corridor certificate
    // proves ordered intervals and final throw owners. The forest below must
    // still visit every operation exactly once.
    const auto cross = [&](std::size_t i) {
      return CrossEvaluationInline(graph, plan, analysis::OpId{static_cast<std::uint32_t>(i)});
    };
    admit(graph.ops.size(), sizeof(EvaluationFrame));
    admit(graph.ops.size(), sizeof(EvaluationVisit));
    step(graph.ops.size()); step(graph.ops.size());
    std::vector<EvaluationFrame> stack(graph.ops.size());
    std::vector<EvaluationVisit> visits(graph.ops.size());
    Require(stack.capacity() <= graph.ops.size() && visits.capacity() <= graph.ops.size(), Error::kResourceLimit);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      const auto& op = graph.ops[i];
      const auto placement = BeforeLogicalFusion(graph, plan, i);
      if (!plan.closed_expressions.empty() && plan.closed_expressions[i]) {
        Require(IsClosedPureValue(graph, {static_cast<std::uint32_t>(i)}) &&
            placement.kind == EvaluationKind::kInline && placement.consumer == plan.closed_expressions[i] &&
            !placement.temporary, Error::kOwnership);
      }
      const bool nested = plan.literals && !plan.literals->nested.empty() && plan.literals->nested[i].has_value();
      Require(nested == (placement.kind == EvaluationKind::kNestedLiteral), Error::kOwnership);
      if (op.life != analysis::ValueLife::kLive) {
        Require(placement.kind == EvaluationKind::kRemoved && !placement.consumer && !placement.temporary,
                Error::kOwnership);
        continue;
      }
      if (std::binary_search(plan.receiver_guards.begin(), plan.receiver_guards.end(), analysis::OpId{static_cast<std::uint32_t>(i)})) {
        Require(placement.kind == EvaluationKind::kReceiverGuard && !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone; continue;
      }
      Require(placement.kind != EvaluationKind::kReceiverGuard, Error::kOwnership);
      const bool cell_guard = plan.cells &&
          IsInitializedCellGuard(graph, *plan.cells, analysis::OpId{static_cast<std::uint32_t>(i)});
      Require(cell_guard == (placement.kind == EvaluationKind::kCellGuard), Error::kOwnership);
      if (cell_guard) {
        Require(!placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone; continue;
      }
      if (projected_op(i)) {
        Require(placement.kind == EvaluationKind::kClassProjection && !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone; continue;
      }
      Require(placement.kind != EvaluationKind::kClassProjection, Error::kOwnership);
      if (placement.kind == EvaluationKind::kElidedPure) {
        Require(flow == EvaluationFlow::kPhased && analysis::CanErasePureOp(op) &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        // Independent local closure check, not a rerun of the mark algorithm.
        // Retained consumers and every phi/snapshot must keep their producer.
        if (op.result) {
          for (auto use = graph.values[op.result->value].first_use; use;
               use = graph.uses[use->value].next) {
            step();
            const auto* owner = std::get_if<analysis::OpId>(&graph.uses[use->value].owner);
            Require(owner && BeforeLogicalFusion(graph, plan, owner->value).kind == EvaluationKind::kElidedPure,
                Error::kOwnership);
          }
        }
        visits[i] = EvaluationVisit::kDone;
        continue;
      }
      if (placement.kind == EvaluationKind::kAlias) {
        Require(op.result &&
            ResolveEvaluationValue(plan, *op.result) != *op.result &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
        continue;
      }
      if (IsEnvironmentState(op.operation)) {
        Require(plan.cells && placement.kind == EvaluationKind::kEnvironment &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
        continue;
      }
      if (std::holds_alternative<analysis::ValueGuard>(op.operation) &&
          !module_index.peers.empty() && module_index.peers[i]) {
        Require(placement.kind == EvaluationKind::kModuleGuard && !placement.consumer && !placement.temporary,
            Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
        continue;
      }
      const auto shape = DescribeEvaluation(op.operation);
      if (const auto error = CheckArgumentSnapshot(graph, op)) return *error;
      Require(shape.has_value(), Error::kUnsupportedOperation);
      Require(shape->Count() == op.use_count && shape->expression == op.result.has_value(), Error::kInvalidGraph);
      if (!IsReceiverField(graph, op) && !IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, {static_cast<std::uint32_t>(i)}) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) {
        // Fresh LiteralPlan proved these definitions unobservable until complete.
        // They execute in object syntax, not as ordinary ordered heap writes;
        // producer snapshots remain pinned before allocation, never duplicated.
        Require(plan.literals && placement.kind == EvaluationKind::kInitializer &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
      } else if (placement.kind == EvaluationKind::kNestedLiteral) {
        Require(plan.literals && plan.literals->nested.size() == graph.ops.size() &&
            plan.literals->nested[i] && placement.consumer == plan.literals->nested[i] &&
            !placement.temporary && op.result, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
      } else if (placement.kind == EvaluationKind::kPhiInput) {
        Require(!pinned[i] && op.result && !placement.temporary && placement.consumer &&
            !phi_inputs.empty() && phi_inputs[i] == placement.consumer && visible[i].first == placement.consumer &&
            !visible[i].multiple, Error::kOwnership);
      } else if (placement.kind == EvaluationKind::kLogicalLeft) {
        Require(!pinned[i] && op.result && !placement.temporary && placement.consumer &&
            !logical_inputs.empty() && logical_inputs[i] == placement.consumer, Error::kOwnership);
      } else if (placement.kind == EvaluationKind::kInline) {
        Require(!pinned[i] || (cross(i) && PhaseCorridors::Aggregate(op.operation)), Error::kOwnership);
        Require(op.result && placement.consumer && !placement.temporary, Error::kOwnership);
        Require(placement.consumer->value < graph.uses.size(), Error::kOwnership);
        const auto& use = graph.uses[placement.consumer->value];
        const auto* owner = std::get_if<analysis::OpId>(&use.owner);
        if (!plan.closed_expressions.empty() && plan.closed_expressions[i]) {
          Require(visible[i].first == placement.consumer && !visible[i].multiple && owner, Error::kOwnership);
        }
        if (!cross(i)) {
          Require(visible[i].first == placement.consumer && !visible[i].multiple && owner &&
              graph.ops[owner->value].block == op.block && graph.ops[owner->value].order > op.order,
              Error::kOwnership);
        }
        Require(!PredicateSnapshot(graph, graph.ops[owner->value], use.operand), Error::kOwnership);
        const auto consumer_shape = DescribeEvaluation(graph.ops[owner->value].operation);
        Require(consumer_shape && !consumer_shape->NeedsSnapshot(use.role), Error::kOrder);
      } else {
        Require(placement.kind == EvaluationKind::kRoot && !placement.consumer, Error::kOwnership);
        const auto visible_uses = visible[i];
        const bool used = visible_uses.first.has_value();
        Require(used == placement.temporary.has_value(), Error::kOwnership);
        if (used) {
          const auto& use = graph.uses[visible_uses.first->value];
          const auto* owner = std::get_if<analysis::OpId>(&use.owner);
          const auto reason = visible_uses.multiple ? TemporaryReason::kMultipleUses
              : !owner || graph.ops[owner->value].block != op.block ? TemporaryReason::kCrossRegionValue
              : PredicateSnapshot(graph, graph.ops[owner->value], use.operand)
                  ? TemporaryReason::kPredicateSnapshot : TemporaryReason::kEffectSnapshot;
          Require(placement.temporary == reason, Error::kOwnership);
        }
      }
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        step();
        const auto& use = graph.uses[op.first_use.value + ordinal];
        if (const auto error = CheckEvaluationUse(graph, use, *shape, ordinal)) return *error;
      }
    }
    std::size_t next_op = 0;
    std::optional<analysis::OpId> fused_guard;
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step();
      const auto& block = graph.blocks[b];
      Require(block.first_op.value == next_op && block.op_count <= graph.ops.size() - next_op,
              Error::kInvalidGraph);
      const auto end = next_op + block.op_count;
      auto expected = next_op;
      const auto skip_pure = [&] {
        if (expected < end && fused_guard && fused_guard->value == expected) {
          Require(BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kModuleGuard, Error::kOrder);
          step(); ++expected; fused_guard.reset();
        }
        while (expected < end && (graph.ops[expected].life != analysis::ValueLife::kLive ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kClassProjection ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kReceiverGuard ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kCellGuard ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kElidedPure ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kAlias ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kInitializer ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kNestedLiteral ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kEnvironment ||
                                  (BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kModuleGuard &&
                                   !module_index.peers.empty() && module_index.peers[expected] &&
                                   (cross(module_index.peers[expected]->value) ||
                                    BeforeLogicalFusion(graph, plan, module_index.peers[expected]->value).kind == EvaluationKind::kNestedLiteral)) ||
                                  cross(expected) ||
                                  !OrderedEvaluation(graph.ops[expected]))) { step(); ++expected; }
      };
      for (auto i = next_op; i < end; ++i) {
        step();
        Require(graph.ops[i].block.value == b && graph.ops[i].order == i - next_op, Error::kInvalidGraph);
        // The phi root moves only across inert trailing control, proved above.
        // Expanding it here checks its subtree's original effect order exactly.
        if (BeforeLogicalFusion(graph, plan, i).kind != EvaluationKind::kRoot &&
            BeforeLogicalFusion(graph, plan, i).kind != EvaluationKind::kPhiInput) continue;
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
            if (IsMemberReceiver(graph, plan, graph.uses[use_id.value]))
              continue;
            const auto* value = std::get_if<analysis::ValueId>(&graph.uses[use_id.value].operand);
            if (!value) continue;
            const auto& definition = graph.values[ResolveEvaluationValue(plan, *value).value];
            // The independently checked phi plan establishes incoming values.
            // A phi use reads its binding; it never expands an input subtree.
            const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
            if (!result) continue;
            const auto& producer = graph.ops[result->op.value];
            const auto placement = BeforeLogicalFusion(graph, plan, result->op.value);
            if (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kLogicalLeft) {
              Require(placement.consumer == use_id && (producer.block == op.block || cross(result->op.value)) &&
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
            // Check the final expression root, not only the immediate
            // consumer: a pure wrapper must not carry a closure past pop/push.
            if (std::holds_alternative<analysis::ValueFunctionCreation>(op.operation) ||
                std::holds_alternative<analysis::ValueClassCreation>(op.operation))
              Require(environment_epochs[frame.op.value] == environment_epochs[i], Error::kOrder);
            if (OrderedEvaluation(op) && !cross(frame.op.value)) {
              skip_pure();
              Require(expected < end && expected == frame.op.value, Error::kOrder);
              ++expected;
              if (std::holds_alternative<analysis::ValueModuleRead>(op.operation) &&
                  !module_index.peers.empty() && module_index.peers[frame.op.value]) {
                if (flow == EvaluationFlow::kPhased) {
                  Require(!fused_guard && !module_index.peers.empty() &&
                      module_index.peers[frame.op.value], Error::kOrder);
                  fused_guard = module_index.peers[frame.op.value];
                } else {
                  // Consume the second original event at this same root, not by
                  // trusting a Language result domain or skipping arbitrary guards.
                  Require(expected < end && !module_index.peers.empty() &&
                      module_index.peers[frame.op.value] == analysis::OpId{static_cast<std::uint32_t>(expected)} &&
                      BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kModuleGuard, Error::kOrder);
                  ++expected;
                }
              }
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
    Require(next_op == graph.ops.size() && !fused_guard, Error::kInvalidGraph);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      Require(graph.ops[i].life != analysis::ValueLife::kLive || visits[i] == EvaluationVisit::kDone,
              Error::kOwnership);
    }
    return budget.work - work;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, EvaluationError> VerifyEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  return VerifyEvaluationForFlow(graph, plan, budget, EvaluationFlow::kNormal);
}
std::variant<std::uint64_t, EvaluationError> VerifyPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  return VerifyEvaluationForFlow(graph, plan, budget, EvaluationFlow::kPhased);
}
}  // namespace abcd::source
