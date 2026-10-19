#include <bit>
#include <new>
#include <stdexcept>

#include "../analysis/value_edits.h"
#include "assignment_values.h"
#include "call_spread_inputs.h"
#include "evaluation_aliases.h"
#include "evaluation_failure.h"
#include "evaluation_flow.h"
#include "evaluation_plan.h"
#include "for_in_evaluation.h"
#include "for_of_evaluation.h"
#include "integer_nan.h"
#include "literal_arm_view.h"
#include "logical_evaluation.h"
#include "logical_regions.h"
#include "module_evaluation.h"
#include "phase_corridor.h"
#include "phase_evaluation.h"
#include "tree_checks.h"

namespace abcd::source {
namespace {
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct EvaluationFrame { analysis::OpId op; std::uint32_t next_operand = 0; };
enum class EvaluationVisit : std::uint8_t { kUnseen, kActive, kDone };
}  // namespace

std::variant<std::uint64_t, EvaluationFailure> VerifyEvaluationForFlow(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::SemanticBudget budget, EvaluationFlow flow, const ForOfEvaluationInput* for_of) {
  EvaluationEvidence witness;
  const auto Require = [&](bool condition, Error error) { if (!condition) throw EvaluationFailure{error, witness}; };
  try {
    Require(plan.flow == flow, Error::kUnsupportedOperation);
    Require(flow == EvaluationFlow::kPhased || (plan.checked_compositions.empty() && plan.checked_failures.empty()), Error::kOwnership);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.operations.size() == graph.ops.size(), Error::kInvalidGraph);
    Require(plan.logical_objects.empty() || plan.logical_objects.size() == graph.ops.size(), Error::kOwnership);
    Require(plan.arguments == graph.arguments, Error::kOwnership);
    Require(plan.guarded_peers.empty() == plan.guarded_reads.empty() &&
        (plan.guarded_peers.empty() || plan.guarded_peers.size() == graph.ops.size()), Error::kOwnership);
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
    const auto protocol = VerifyForInEvaluation(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&protocol)) return *error;
    step(std::get<std::uint64_t>(protocol));
    const auto of=VerifyForOfEvaluation(graph,for_of,plan,{bytes,work});
    if(const auto* error=std::get_if<EvaluationFailure>(&of))return *error;
    step(std::get<std::uint64_t>(of));
    bool has_notification=false;
    for(const auto& op:graph.ops){step();has_notification|=std::holds_alternative<analysis::ValueConcurrentNotification>(op.operation);}
    Require(has_notification==plan.concurrent.has_value(),Error::kOwnership);
    if(plan.concurrent) {
      auto checked=VerifyConcurrentCompletion(graph,*plan.concurrent,{bytes,work});
      if(const auto* failure=std::get_if<ConcurrentFailure>(&checked)) {
        EvaluationFailure e{failure->kind==ConcurrentError::kResourceLimit?Error::kResourceLimit:
            failure->kind==ConcurrentError::kWorkLimit?Error::kWorkLimit:Error::kOwnership};
        e.evaluation.concurrent=*failure;if(failure->operation)e.evaluation.location=OperationLocation(graph,*failure->operation);return e;
      }step(std::get<std::uint64_t>(checked));
    }
    if (!plan.guarded_reads.empty()) {
      Require(flow == EvaluationFlow::kPhased && plan.cells, Error::kOwnership);
      auto proof = analysis::ProveGuardedReads(graph, {bytes, work});
      if (const auto* error = std::get_if<analysis::GuardedReadError>(&proof)) {
        if (*error == analysis::GuardedReadError::kResourceLimit) return Error::kResourceLimit;
        if (*error == analysis::GuardedReadError::kWorkLimit) return Error::kWorkLimit;
        if (*error == analysis::GuardedReadError::kCatchState) return Error::kCatchState;
        return Error::kOwnership;
      }
      const auto& evidence = std::get<analysis::GuardedReads>(proof);
      admit(evidence.allocated_bytes, 1); step(evidence.work);
      std::size_t found = 0, peers = 0;
      for (std::size_t i = 0; i < plan.guarded_reads.size(); ++i) {
        step(); const auto& pair = plan.guarded_reads[i];
        Require(i == 0 || plan.guarded_reads[i - 1].read < pair.read, Error::kOwnership);
        while (found < evidence.reads.size() && evidence.reads[found].read < pair.read) { step(); ++found; }
        Require(found < evidence.reads.size() && evidence.reads[found] == pair &&
            GuardedPeer(plan, pair.read) == pair.guard && GuardedPeer(plan, pair.guard) == pair.read &&
            GuardedInput(plan, pair.read) && plan.guarded_peers[pair.guard.value]->role == GuardedReadRole::kGuard, Error::kOwnership);
        const auto cell = plan.cells->cells.operations[pair.read.value];
        Require(cell && std::holds_alternative<IncomingCell>(plan.cells->cells.cells[cell->value].storage), Error::kOwnership);
        const auto& read = plan.operations[pair.read.value]; const auto& guard = plan.operations[pair.guard.value];
        Require(((read.kind == EvaluationKind::kRoot && read.temporary == TemporaryReason::kCrossRegionValue && !read.consumer) ||
            (read.kind == EvaluationKind::kInline && read.consumer && !read.temporary && !plan.checked_compositions.empty())) &&
            guard.kind == EvaluationKind::kGuardedCellRead && !guard.consumer && !guard.temporary, Error::kOwnership);
      }
      for (const auto& peer : plan.guarded_peers) { step(); peers += peer.has_value(); }
      Require(peers == 2 * plan.guarded_reads.size(), Error::kOwnership);
    }
    const auto availability = CheckEvaluationFlow(graph, flow, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&availability)) return *error;
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
      EvaluationInputSite failure;
      const auto analyzed = AnalyzeHoleProtocol(graph, {bytes, work}, &failure);
      const auto* protocol = std::get_if<HoleProtocol>(&analyzed);
      if (!protocol) {
        witness.plan_cause = std::get<HoleProtocolError>(analyzed);
        witness.input_check = failure.check; witness.use = failure.use; witness.value = failure.value;
        if (failure.operation) witness.location = OperationLocation(graph, *failure.operation);
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
    if (const auto* error = std::get_if<EvaluationFailure>(&aliases)) return *error;
    step(std::get<std::uint64_t>(aliases));
    const auto normalized = VerifyIntegerNaNs(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&normalized)) return *error;
    step(std::get<std::uint64_t>(normalized));
    auto indexed_modules = IndexModuleEvaluations(graph, plan.modules, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&indexed_modules)) return *error;
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
    bool has_definitions = false, has_spread = false;
    bool has_cells = plan.cells && (!plan.cells->cells.demands.empty() || !plan.cells->cells.entry_demands.empty());
    bool has_arguments = false;
    std::size_t next_function = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      if (const auto failure = NameSettingRefusal(graph, {static_cast<std::uint32_t>(i)})) return *failure;
      if (const auto failure = CallSpreadRefusal(graph, {static_cast<std::uint32_t>(i)}, plan.call_spread_contract)) return *failure;
      const auto shape = op.life == analysis::ValueLife::kLive ? DescribeEvaluation(op.operation) : std::nullopt;
      has_arguments |= shape && shape->NeedsArgumentFrame();
      if (op.life == analysis::ValueLife::kLive && IsEnvironmentState(op.operation)) ++environment_epoch;
      environment_epochs[i] = environment_epoch;
      has_spread |= op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueArraySpread>(op.operation);
      if (projected_op(i)) if (const auto failure = NameSettingRefusal(graph,
          {static_cast<std::uint32_t>(i)}, true)) return *failure;
      step(); has_definitions |= !projected_op(i) && op.life == analysis::ValueLife::kLive &&
          (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation));
      has_cells |= op.life == analysis::ValueLife::kLive && NeedsCellEvaluation(op.operation);
      if (op.life == analysis::ValueLife::kLive && !projected_op(i) && std::holds_alternative<analysis::ValueFunctionCreation>(op.operation)) {
        Require(next_function < plan.functions.size() && plan.functions[next_function++].creation.value == i, Error::kOwnership);
      }
    }
    Require(next_function == plan.functions.size(), Error::kOwnership);
    Require(has_arguments == graph.arguments.has_value(), Error::kOwnership);
    Require((has_definitions || has_spread) == plan.literals.has_value(), Error::kOwnership);
    Require(has_cells == plan.cells.has_value(), Error::kOwnership);
    if (plan.cells) {
      Require(plan.cells->initialization.closure_writes == (plan.functions.empty()
          ? plan.closure_writes : analysis::ClosureWrites::kLanguage), Error::kOwnership);
      const auto verify_cells = flow == EvaluationFlow::kPhased ? VerifyPhasedCellEvaluation : VerifyCellEvaluation;
      EvaluationInputSite failure;
      const auto checked = verify_cells(graph, *plan.cells, {bytes, work}, &failure);
      if (const auto* error = std::get_if<CellEvaluationError>(&checked)) {
        witness.plan_cause = *error;
        witness.input_check = failure.check; witness.use = failure.use; witness.value = failure.value;
        if (failure.operation) witness.location = OperationLocation(graph, *failure.operation);
        if (*error == CellEvaluationError::kResourceLimit) return EvaluationFailure{Error::kResourceLimit, witness};
        if (*error == CellEvaluationError::kWorkLimit) return EvaluationFailure{Error::kWorkLimit, witness};
        return EvaluationFailure{Error::kUnsupportedOperation, witness};
      }
      step(std::get<std::uint64_t>(checked));
    }
    for (const auto& function : plan.functions) {
      step();
      for (const auto cell : function.captures) {
        step(); Require(plan.cells && cell.value < plan.cells->cells.cells.size(), Error::kOwnership);
      }
    }
    Require(has_spread == (ArrayInitializations(plan) != nullptr), Error::kOwnership);
    std::vector<std::optional<analysis::OpId>> literal_projection;
    auto projected_literals = std::span<const std::optional<analysis::OpId>>(plan.class_projection.owners);
    TreeCheckBudget spread_budget({bytes,work});
    const auto spread_inputs = IndexCallSpreadInputs(graph, plan, spread_budget);
    admit(spread_budget.Bytes(),1); step(spread_budget.Work());
    if (const auto* arrays = ArrayInitializations(plan)) {
      const auto proof = VerifyArrayInitializationStructure(graph, *arrays, {bytes, work}, &plan);
      if (const auto* error = std::get_if<ArrayInitializationError>(&proof)) return
          *error == ArrayInitializationError::kResourceLimit ? Error::kResourceLimit :
          *error == ArrayInitializationError::kWorkLimit ? Error::kWorkLimit : Error::kOwnership;
      step(std::get<std::uint64_t>(proof));
      admit(graph.ops.size(), sizeof(std::optional<analysis::OpId>)); literal_projection.resize(graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        step(); literal_projection[i] = arrays->owners[i] ? arrays->owners[i] :
            (plan.class_projection.owners.empty() ? std::nullopt : plan.class_projection.owners[i]);
      }
      projected_literals = literal_projection;
      for (const auto& array : arrays->arrays) {
        step(); literal_allocations[array.allocation.value] = 1; pinned[array.allocation.value] =
            spread_inputs.empty() || !spread_inputs[array.allocation.value];
      }
      for (const auto& item : arrays->steps) if (item.value) {
        step(); const auto operand = ResolveEvaluationOperand(plan, graph.uses[item.value->value].operand);
        if (const auto* value = std::get_if<analysis::ValueId>(&operand))
          if (const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin)) pinned[result->op.value] = 1;
      }
    }
    if (plan.literals) {
      EvaluationInputSite failure;
      const auto checked = flow == EvaluationFlow::kPhased ?
          VerifyPhasedLiteralPlan(graph, *plan.literals, plan, {bytes, work}, module_index.peers, projected_literals, &failure) :
          VerifyLiteralPlan(graph, *plan.literals, {bytes, work}, module_index.peers, projected_literals, plan.receiver_guards, plan.cells ? &*plan.cells : nullptr, &failure);
      if (const auto* error = std::get_if<LiteralError>(&checked)) {
        witness.plan_cause = *error;
        witness.input_check = failure.check; witness.use = failure.use; witness.value = failure.value;
        if (failure.operation) witness.location = OperationLocation(graph, *failure.operation);
        if (*error == LiteralError::kResourceLimit) return EvaluationFailure{Error::kResourceLimit, witness};
        if (*error == LiteralError::kWorkLimit) return EvaluationFailure{Error::kWorkLimit, witness};
        return EvaluationFailure{Error::kUnsupportedOperation, witness};
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
        if (IsStagedLiteral(&*plan.literals, id)) continue;
        const auto rhs = ResolveEvaluationOperand(plan, graph.uses[graph.ops[id.value].first_use.value + 1].operand);
        if (const auto* value = std::get_if<analysis::ValueId>(&rhs))
          if (const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin))
            pinned[result->op.value] = 1;
      }
    }
    {
      TreeCheckBudget arm_budget({bytes, work});
      const auto expected = IndexNestedLiteralInputs(graph, plan, arm_budget);
      Require(expected == plan.literal_arm_inputs, Error::kOwnership);
      for (std::size_t i = 0; i < expected.size(); ++i) {
        arm_budget.Step();
        if (expected[i]) {
          pinned[i] = 0;
          literal_allocations[i] = 1;
        }
      }
      admit(arm_budget.Bytes(), 1);
      step(arm_budget.Work());
    }
    // Recompute actual observations after identity projection, including uses
    // transported through phi/copy aliases. Each original use is visited once.
    TreeCheckBudget use_budget({bytes,work});
    VerifyAssignmentValues(graph, plan, use_budget);
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
          : VerifyPhasedPhiCopies(graph, *plan.phis, {bytes, work}, plan.for_of ? std::span<const ForOfUseDisposition>(plan.for_of->uses) : std::span<const ForOfUseDisposition>{});
      if (const auto* error = std::get_if<PhiCopyError>(&checked)) {
        if (*error == PhiCopyError::kResourceLimit) return Error::kResourceLimit;
        if (*error == PhiCopyError::kWorkLimit) return Error::kWorkLimit;
        return Error::kLivePhi;
      }
      step(std::get<std::uint64_t>(checked));
    }
    if (plan.exceptions) {
      const auto checked = VerifyExceptionCopies(graph, *plan.phis, *plan.exceptions, {bytes, work},plan.for_of ? std::span<const ForOfUseDisposition>(plan.for_of->uses) : std::span<const ForOfUseDisposition>{});
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
      phi_inputs = IndexPhiExpressionInputs(graph, *plan.phis, module_index.peers, literal_ends, phi_budget, plan.aliases);
      admit(phi_budget.Bytes(), 1); step(phi_budget.Work());
    }
    TreeCheckBudget logical_budget({bytes, work});
    const auto logical_inputs = FindLogicalLeftInputs(graph, plan, logical_budget);
    admit(logical_budget.Bytes(), 1); step(logical_budget.Work());
    TreeCheckBudget fusion_budget({bytes, work});
    VerifyLogicalValues(graph, plan, fusion_budget);
    // A staged indexed DefineOwnProperty has no source indexed-Set fallback.
    // The typed commit table proves construction; the independently verified
    // complete aggregate projection must additionally own every commit.
    if (plan.literals)
      for (const auto& object : plan.literals->objects) {
        fusion_budget.Step();
        if (!object.indexed_first) continue;
        if (plan.logical_objects.empty() ||
            !plan.logical_objects[object.allocation.value]) {
          auto evidence = EvaluationAt(graph, plan, object.allocation,
                                       EvaluationObligation::kProjection);
          evidence.plan_cause = LiteralError::kDefinition;
          evidence.input_check = EvaluationInputCheck::kLiteralStagedStorage;
          throw EvaluationFailure{Error::kUnsupportedOperation, evidence};
        }
        Require(plan.logical_objects[object.allocation.value] ==
                    plan.literals->definitions[object.first + object.count - 1],
                Error::kOwnership);
        for (std::uint32_t i = 0; i < object.count; ++i) {
          fusion_budget.Step();
          Require(plan.operations[plan.literals->definitions[object.first + i]
                                      .value]
                          .kind == EvaluationKind::kInitializer,
                  Error::kOwnership);
        }
      }

    admit(fusion_budget.Bytes(), 1); step(fusion_budget.Work());
    const auto module_copies = VerifyModuleCatchCopies(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&module_copies)) return *error;
    step(std::get<std::uint64_t>(module_copies));
    const auto expressions = VerifyPhaseExpressions(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&expressions)) return *error;
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
      witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(i)}, EvaluationObligation::kPlan);
      const auto placement = BeforeLogicalFusion(graph, plan, i);
      if (!plan.closed_expressions.empty() && plan.closed_expressions[i]) {
        Require(IsClosedPureValue(graph, {static_cast<std::uint32_t>(i)}) &&
            placement.kind == EvaluationKind::kInline && placement.consumer == plan.closed_expressions[i] &&
            !placement.temporary, Error::kOwnership);
      }
      if(op.life==analysis::ValueLife::kLive&&std::holds_alternative<analysis::ValueConcurrentNotification>(op.operation)) {
        Require(plan.concurrent&&plan.concurrent->members[i]&&placement.kind==EvaluationKind::kConcurrentNotification&&!placement.consumer&&!placement.temporary,Error::kOwnership);
        visits[i]=EvaluationVisit::kDone;continue;
      }
      Require(placement.kind!=EvaluationKind::kConcurrentNotification,Error::kOwnership);
      if (ForOfOperation(plan,{static_cast<std::uint32_t>(i)})) {
        Require(placement.kind==EvaluationKind::kForOfEvent && !placement.consumer && !placement.temporary,Error::kOwnership);
        visits[i]=EvaluationVisit::kDone;continue;
      }
      Require(placement.kind!=EvaluationKind::kForOfEvent,Error::kOwnership);
      if (ForInOperation(plan, {static_cast<std::uint32_t>(i)})) {
        Require(placement.kind == EvaluationKind::kForInEvent && !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone; continue;
      }
      Require(placement.kind != EvaluationKind::kForInEvent, Error::kOwnership);
      const bool nested = NestedLiteralUse(plan, {static_cast<std::uint32_t>(i)}).has_value();
      Require(nested == (placement.kind == EvaluationKind::kNestedLiteral), Error::kOwnership);
      if (op.life != analysis::ValueLife::kLive) {
        Require(placement.kind == EvaluationKind::kRemoved && !placement.consumer && !placement.temporary,
                Error::kOwnership);
        continue;
      }
      if (IsArrayInitialization(plan, {static_cast<std::uint32_t>(i)})) {
        Require(placement.kind == (IsArrayCursor(plan, {static_cast<std::uint32_t>(i)}, graph) ?
            EvaluationKind::kArrayCursor : EvaluationKind::kInitializer) && !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone; continue;
      }
      Require(placement.kind != EvaluationKind::kArrayCursor, Error::kOwnership);
      if (std::binary_search(plan.receiver_guards.begin(), plan.receiver_guards.end(), analysis::OpId{static_cast<std::uint32_t>(i)})) {
        Require(placement.kind == EvaluationKind::kReceiverGuard && !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone; continue;
      }
      Require(placement.kind != EvaluationKind::kReceiverGuard, Error::kOwnership);
      if (const auto peer = GuardedPeer(plan, {static_cast<std::uint32_t>(i)})) {
        if (!GuardedInput(plan, {static_cast<std::uint32_t>(i)})) visits[i] = EvaluationVisit::kDone;
        continue;
      }
      Require(placement.kind != EvaluationKind::kGuardedCellRead, Error::kOwnership);
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
      if (!IsReceiverField(graph, op) && (!IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, {static_cast<std::uint32_t>(i)}) || placement.kind == EvaluationKind::kInitializer) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) {
        // Fresh LiteralPlan proved these definitions unobservable until complete.
        // They execute in object syntax, not as ordinary ordered heap writes;
        // producer snapshots remain pinned before allocation, never duplicated.
        Require(plan.literals && placement.kind == EvaluationKind::kInitializer &&
            !placement.consumer && !placement.temporary, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
      } else if (placement.kind == EvaluationKind::kNestedLiteral) {
        Require(NestedLiteralUse(plan, {static_cast<std::uint32_t>(i)}) &&
            placement.consumer == NestedLiteralUse(plan, {static_cast<std::uint32_t>(i)}) &&
            !placement.temporary && op.result, Error::kOwnership);
        visits[i] = EvaluationVisit::kDone;
      } else if (placement.kind == EvaluationKind::kPhiInput) {
        witness.obligation = EvaluationObligation::kPhiInput;
        Require(
            !pinned[i] && op.result && !placement.temporary &&
                placement.consumer &&
                ((!phi_inputs.empty() && phi_inputs[i] == placement.consumer) ||
                 (!plan.literal_arm_inputs.empty() &&
                  plan.literal_arm_inputs[i] == placement.consumer)) &&
                visible[i].first == placement.consumer && !visible[i].multiple,
            Error::kOwnership);
      } else if (placement.kind == EvaluationKind::kLogicalLeft) {
        witness.obligation = EvaluationObligation::kLogicalInput;
        Require(!pinned[i] && op.result && !placement.temporary && placement.consumer &&
            !logical_inputs.empty() && logical_inputs[i] == placement.consumer, Error::kOwnership);
      } else if (placement.kind == EvaluationKind::kInline) {
        witness.obligation = EvaluationObligation::kInlineConsumer;
        Require(!pinned[i] || (cross(i) && PhaseCorridors::Aggregate(op.operation)), Error::kOwnership);
        Require((op.result || (i < plan.assignment_consumers.size() && plan.assignment_consumers[i])) && placement.consumer && !placement.temporary, Error::kOwnership);
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
        witness.obligation = EvaluationObligation::kRootTemporary;
        Require(placement.kind == EvaluationKind::kRoot && !placement.consumer, Error::kOwnership);
        witness.obligation = EvaluationObligation::kTemporaryPresence;
        const auto visible_uses = visible[i];
        const bool used = visible_uses.first.has_value();
        witness.expected_consumer = visible_uses.first;
        Require(used == placement.temporary.has_value(), Error::kOwnership);
        if (used) {
          const auto& use = graph.uses[visible_uses.first->value];
          const auto* owner = std::get_if<analysis::OpId>(&use.owner);
          const auto reason = visible_uses.multiple ? TemporaryReason::kMultipleUses
              : !owner || graph.ops[owner->value].block != op.block ? TemporaryReason::kCrossRegionValue
              : PredicateSnapshot(graph, graph.ops[owner->value], use.operand)
                  ? TemporaryReason::kPredicateSnapshot : TemporaryReason::kEffectSnapshot;
          witness.obligation = EvaluationObligation::kTemporaryReason;
          witness.expected_temporary = reason;
          Require(placement.temporary == reason, Error::kOwnership);
        }
      }
      for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        step();
        const auto& use = graph.uses[op.first_use.value + ordinal];
        if (const auto error = CheckEvaluationUse(graph, use, *shape, ordinal)) return *error;
      }
    }
    witness = {};
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
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kArrayCursor ||
                                  ForInOperation(plan, {static_cast<std::uint32_t>(expected)}) || ForOfOperation(plan,{static_cast<std::uint32_t>(expected)}) ||
                                  BeforeLogicalFusion(graph,plan,expected).kind == EvaluationKind::kConcurrentNotification ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kClassProjection ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kReceiverGuard ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kCellGuard ||
                                  BeforeLogicalFusion(graph, plan, expected).kind == EvaluationKind::kGuardedCellRead ||
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
        witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(i)}, EvaluationObligation::kDuplicateVisit);
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
            const auto result = EvaluationProducer(graph, plan, use_id);
            if (!result) continue;
            witness = EvaluationAt(graph, plan, *result, EvaluationObligation::kOperandOwnership);
            witness.use = witness.expected_consumer = use_id;
            witness.expected_owner = OperationLocation(graph, frame.op);
            const auto& producer = graph.ops[result->value];
            const auto placement = BeforeLogicalFusion(graph, plan, result->value);
            if (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kLogicalLeft) {
              Require(placement.consumer == use_id && (producer.block == op.block || cross(result->value)) &&
                      visits[result->value] == EvaluationVisit::kUnseen, Error::kOwnership);
              Require(depth < stack.size(), Error::kResourceLimit);
              visits[result->value] = EvaluationVisit::kActive;
              stack[depth++] = {*result, 0};
            } else {
              if (ForOfOperation(plan,*result)) {
                // Only the success value is an ordinary source result. Internal
                // iterator/state dependencies cannot enter a body expression.
                const auto member=plan.for_of->identity.operations[result->value];
                Require(member && member->role==ForOfRole::kValue,Error::kOwnership);continue;
              }
              if (ForInOperation(plan, *result)) {
                // The closed protocol proof checked all external key uses on
                // the not-done edge. No ordinary expression/temporary is claimed.
                const auto operand = ResolveEvaluationOperand(plan, graph.uses[use_id.value].operand);
                const auto* value = std::get_if<analysis::ValueId>(&operand);
                Require(value && value->value < plan.for_in->values.size(), Error::kOwnership);
                const auto member = plan.for_in->values[value->value];
                Require(member && member->role == ForInRole::kNextResult, Error::kOwnership);
                continue;
              }
              Require(placement.kind == EvaluationKind::kRoot, Error::kOwnership);
              Require(producer.block != op.block || visits[result->value] == EvaluationVisit::kDone,
                      Error::kUnavailableValue);
            }
          } else {
            witness = EvaluationAt(graph, plan, frame.op, EvaluationObligation::kPlan);
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
      witness = {}; witness.location.block = analysis::WorkBlockId{static_cast<std::uint32_t>(b)};
      if (expected < end) witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(expected)}, EvaluationObligation::kPlan);
      Require(expected == end, Error::kOrder);
      next_op = end;
    }
    witness = {};
    Require(next_op == graph.ops.size() && !fused_guard, Error::kInvalidGraph);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(i)}, EvaluationObligation::kLiveCoverage);
      Require(graph.ops[i].life != analysis::ValueLife::kLive || visits[i] == EvaluationVisit::kDone,
              Error::kOwnership);
    }
    return budget.work - work;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const EvaluationFailure& error) { return error;
  } catch (Error error) { return EvaluationFailure{error, witness};
  } catch (LiteralEventError) {
    return Error::kOwnership;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
std::variant<std::uint64_t, EvaluationFailure> VerifyEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  return VerifyEvaluationForFlow(graph, plan, budget, EvaluationFlow::kNormal);
}
std::variant<std::uint64_t, EvaluationFailure> VerifyPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget, const ForOfEvaluationInput* for_of) {
  return VerifyEvaluationForFlow(graph, plan, budget, EvaluationFlow::kPhased, for_of);
}
}  // namespace abcd::source
