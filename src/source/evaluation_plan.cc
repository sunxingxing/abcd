#include "evaluation_plan.h"
#include "for_in_evaluation.h"
#include "evaluation_failure.h"
#include "assignment_values.h"
#include "evaluation_flow.h"
#include "evaluation_liveness.h"
#include "evaluation_aliases.h"
#include "integer_nan.h"
#include "module_evaluation.h"
#include "phase_evaluation.h"
#include "phase_composition.h"
#include "logical_regions.h"
#include "logical_evaluation.h"
#include "tree_checks.h"

#include <limits>
#include <bit>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct EffectInterval { std::uint32_t count = 0; std::uint32_t end = 0; };
std::variant<EvaluationPlan, EvaluationFailure> BuildEvaluationAttempt(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules,
    EvaluationFlow flow, std::span<const CellDemand> demands, std::span<const ClassInitializerCall> initializers,
    analysis::ClosureWrites closure_writes, std::span<const ClassDefinitionSite> definitions, std::span<const analysis::OpId> receiver_guards, std::span<const IncomingCell> entry_demands = {}, const ClassSelfOrder* self_order = nullptr, std::span<const ClassPrototypeRead> prototype_reads = {}, ArraySourceInput array_source = {}, bool assignments = true, bool* proposed = nullptr, std::span<const analysis::GuardedRead> guarded = {}) {
  EvaluationEvidence witness;
  const auto Require = [&](bool condition, Error error) { if (!condition) throw EvaluationFailure{error, witness}; };
  auto work = budget.work; auto bytes = budget.bytes;
  try {
    Require(incoming == IncomingCells::kReject || incoming == IncomingCells::kConditional, Error::kUnsupportedOperation);

    const auto step = [&](std::size_t count = 1) {
      Require(count <= work, Error::kWorkLimit);
      work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= bytes / width, Error::kResourceLimit);
      bytes -= count * width;
    };
    const auto availability = CheckEvaluationFlow(graph, flow, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&availability)) return *error;
    step(std::get<std::uint64_t>(availability));
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
    plan.flow = flow;
    bool has_notification=false;
    for(const auto& op:graph.ops){step();has_notification|=std::holds_alternative<analysis::ValueConcurrentNotification>(op.operation);}
    if(has_notification) {
      auto proof=BuildConcurrentCompletion(graph,{bytes,work});
      if(const auto* failure=std::get_if<ConcurrentFailure>(&proof)) {
        EvaluationFailure e{failure->kind==ConcurrentError::kResourceLimit?Error::kResourceLimit:
            failure->kind==ConcurrentError::kWorkLimit?Error::kWorkLimit:Error::kOwnership};
        e.evaluation.concurrent=*failure;if(failure->operation)e.evaluation.location=OperationLocation(graph,*failure->operation);return e;
      }
      plan.concurrent=std::get<ConcurrentCompletion>(std::move(proof));admit(plan.concurrent->allocated_bytes,1);step(plan.concurrent->work);
    }

    bool has_enumeration = false;
    for (const auto& op : graph.ops) {
      step(); has_enumeration |= op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueEnumeration>(op.operation);
    }
    // Increment I connects the two phased production callers. Normal-flow
    // enumeration keeps its existing refusal until its event-order adapter is
    // implemented; raw protocol proofs alone never authorize expression fusion.
    if (has_enumeration && flow == EvaluationFlow::kPhased) {
      analysis::SemanticBudget left{bytes, work};
      if (const auto error = BuildForInEvaluation(graph, plan, left)) return *error;
      bytes = left.bytes; work = left.work;
    }
    if (!guarded.empty()) {
      Require(flow == EvaluationFlow::kPhased, Error::kOwnership);
      admit(guarded.size(), sizeof(analysis::GuardedRead));
      admit(graph.ops.size(), sizeof(std::optional<GuardedReadPeer>));
      plan.guarded_reads.assign(guarded.begin(), guarded.end());
      plan.guarded_peers.resize(graph.ops.size());
      admit(plan.guarded_reads.capacity() - guarded.size(), sizeof(analysis::GuardedRead));
      admit(plan.guarded_peers.capacity() - graph.ops.size(), sizeof(std::optional<GuardedReadPeer>));
      for (const auto& pair : guarded) {
        step(); Require(pair.read != pair.guard && pair.read.value < graph.ops.size() && pair.guard.value < graph.ops.size() &&
            !plan.guarded_peers[pair.read.value] && !plan.guarded_peers[pair.guard.value], Error::kOwnership);
        plan.guarded_peers[pair.read.value] = GuardedReadPeer{pair.guard, GuardedReadRole::kRead};
        plan.guarded_peers[pair.guard.value] = GuardedReadPeer{pair.read, GuardedReadRole::kGuard};
      }
    }
    plan.closure_writes = closure_writes;
    plan.arguments = graph.arguments;
    Require(receiver_guards.empty() || flow == EvaluationFlow::kNormal, Error::kOwnership);
    admit(receiver_guards.size(), sizeof(analysis::OpId));
    plan.receiver_guards.assign(receiver_guards.begin(), receiver_guards.end());
    admit(plan.receiver_guards.capacity() - plan.receiver_guards.size(), sizeof(analysis::OpId));
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

    if (self_order) {
      Require(!self_order->Stores(graph).empty(),Error::kOwnership);
      admit(self_order->AllocatedBytes(),1); plan.self_order = *self_order;
    }
    auto projected = ProjectClassMembers(graph, initializers, {bytes, work}, definitions, plan.self_order ? &*plan.self_order : nullptr, prototype_reads);
    if (const auto* error = std::get_if<ClassInitializerError>(&projected))
      return *error == ClassInitializerError::kResourceLimit ? Error::kResourceLimit :
          *error == ClassInitializerError::kWorkLimit ? Error::kWorkLimit : Error::kOwnership;
    plan.class_projection = std::get<ClassProjection>(std::move(projected));
    admit(plan.class_projection.allocated_bytes, 1); step(plan.class_projection.work);
    admit(initializers.size(), sizeof(ClassInitializerCall));
    plan.class_initializers.assign(initializers.begin(), initializers.end());
    admit(definitions.size(), sizeof(ClassDefinitionSite));
    plan.class_definitions.assign(definitions.begin(), definitions.end());
    admit(plan.class_definitions.capacity() - definitions.size(), sizeof(ClassDefinitionSite));
    admit(plan.class_initializers.capacity() - initializers.size(), sizeof(ClassInitializerCall));
    admit(prototype_reads.size(), sizeof(ClassPrototypeRead));
    plan.class_prototype_reads.assign(prototype_reads.begin(), prototype_reads.end());
    admit(plan.class_prototype_reads.capacity() - prototype_reads.size(), sizeof(ClassPrototypeRead));
    const auto projected_op = [&](std::size_t i) {
      return !plan.class_projection.owners.empty() && plan.class_projection.owners[i].has_value();
    };
    Require(functions.size() <= graph.ops.size(), Error::kOwnership);
    admit(functions.size(), sizeof(FunctionBinding));
    plan.functions.reserve(functions.size());
    admit(plan.functions.capacity() - functions.size(), sizeof(FunctionBinding));
    for (const auto& function : functions) {
      step();
      admit(function.captures.size(), sizeof(CellId));
      FunctionBinding owned{function.creation, function.child};
      owned.captures.reserve(function.captures.size());
      admit(owned.captures.capacity() - function.captures.size(), sizeof(CellId));
      for (const auto cell : function.captures) { step(); owned.captures.push_back(cell); }
      plan.functions.push_back(std::move(owned));
    }
    Require(modules.size() <= graph.ops.size(), Error::kOwnership);
    admit(modules.size(), sizeof(ModuleEvaluation));
    plan.modules.reserve(modules.size());
    admit(plan.modules.capacity() - modules.size(), sizeof(ModuleEvaluation));
    for (const auto& module : modules) { step(); plan.modules.push_back(module); }
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
    bool has_definitions = false, has_spread = false;
    bool has_cells = !demands.empty() || !entry_demands.empty();
    bool has_arguments = false;
    std::size_t next_function = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      const auto shape = op.life == analysis::ValueLife::kLive ? DescribeEvaluation(op.operation) : std::nullopt;
      has_arguments |= shape && shape->NeedsArgumentFrame();
      has_spread |= op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueArraySpread>(op.operation);
      step(); has_definitions |= !projected_op(i) && op.life == analysis::ValueLife::kLive &&
          (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation));
      has_cells |= op.life == analysis::ValueLife::kLive && NeedsCellEvaluation(op.operation);
      if (op.life == analysis::ValueLife::kLive && !projected_op(i) && std::holds_alternative<analysis::ValueFunctionCreation>(op.operation)) {
        Require(next_function < functions.size() && functions[next_function++].creation.value == i, Error::kOwnership);
        // Creation is ordered, not intrinsically a statement. Environment
        // fences below prevent its subtree from crossing implicit scope state.
      }
    }
    Require(next_function == functions.size(), Error::kOwnership);
    Require(has_arguments == graph.arguments.has_value(), Error::kOwnership);
    bool has_holes = false;
    for (const auto& op : graph.ops) { step(); has_holes |= std::holds_alternative<analysis::ValueHole>(op.operation); }
    if (has_holes) {
      auto analyzed = AnalyzeHoleProtocol(graph, {bytes, work});
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
      admit(protocol->allocated_bytes, 1); step(protocol->work);
      plan.holes = std::get<HoleProtocol>(std::move(analyzed));
    }
    if (has_cells) {
      const auto writes = functions.empty() ? closure_writes : analysis::ClosureWrites::kLanguage;
      auto built = flow == EvaluationFlow::kPhased
          ? BuildPhasedCellEvaluation(graph, {bytes, work}, incoming, writes, demands)
          : BuildCellEvaluation(graph, {bytes, work}, incoming, writes, demands, entry_demands);
      if (const auto* error = std::get_if<CellEvaluationError>(&built)) {
        if (*error == CellEvaluationError::kResourceLimit) return Error::kResourceLimit;
        if (*error == CellEvaluationError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      plan.cells = std::get<CellEvaluation>(std::move(built));
      admit(plan.cells->allocated_bytes, 1); step(plan.cells->work);
    }
    if (has_phi) {
      auto copies = flow == EvaluationFlow::kNormal ? BuildPhiCopies(graph, {bytes, work})
          : BuildPhasedPhiCopies(graph, {bytes, work}, plan.holes ? PhiDomain::kHoleTransport : PhiDomain::kLanguage, plan.for_in ? &*plan.for_in : nullptr);
      auto* phis = std::get_if<PhiCopies>(&copies);
      if (!phis) {
        const auto error = std::get<PhiCopyError>(copies);
        if (error == PhiCopyError::kResourceLimit) return Error::kResourceLimit;
        if (error == PhiCopyError::kWorkLimit) return Error::kWorkLimit;
        return Error::kLivePhi;
      }
      admit(phis->allocated_bytes, 1); step(phis->work);
      plan.phis = std::move(*phis);
      if (flow == EvaluationFlow::kPhased) {
        auto staged = BuildExceptionCopies(graph, *plan.phis, {bytes, work});
        if (const auto* error = std::get_if<ExceptionCopyError>(&staged)) {
          if (*error == ExceptionCopyError::kResourceLimit) return Error::kResourceLimit;
          if (*error == ExceptionCopyError::kWorkLimit) return Error::kWorkLimit;
          return Error::kLivePhi;
        }
        plan.exceptions = std::get<ExceptionCopies>(std::move(staged));
        admit(plan.exceptions->allocated_bytes, 1); step(plan.exceptions->work);
      }
    }
    admit(graph.ops.size(), sizeof(OpEvaluation));
    admit(graph.ops.size(), sizeof(EffectInterval));
    step(graph.ops.size()); step(graph.ops.size());
    plan.operations.resize(graph.ops.size());
    if (plan.cells) {
      // Classify proved nonthrowing cell checks before literal/corridor planning.
      // Phased initialization uses completed-store facts, including catch paths.
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        step();
        if (!GuardedPeer(plan, {static_cast<std::uint32_t>(i)}) && IsInitializedCellGuard(graph, *plan.cells, analysis::OpId{static_cast<std::uint32_t>(i)}))
          plan.operations[i].kind = EvaluationKind::kCellGuard;
      }
    }
    std::vector<EffectInterval> groups(graph.ops.size());
    Require(plan.operations.capacity() <= graph.ops.size() && groups.capacity() <= graph.ops.size(),
            Error::kResourceLimit);
    if (flow == EvaluationFlow::kPhased) {
      // Normal inputs already use verified IR simplification. Phase inputs
      // keep exact import correspondence; elide only in the source projection.
      const auto live = ElidePureEvaluations(graph, plan, {bytes, work});
      if (const auto* error = std::get_if<EvaluationFailure>(&live)) return *error;
      const auto cost = std::get<EvaluationLivenessCost>(live);
      admit(cost.bytes, 1); step(cost.work);
    }
    // Copy identity belongs to the source projection in either flow. Exact
    // imports retain transport anchors; simplification is an optimization,
    // never a prerequisite for assigning one computation its original site.
    const auto aliases = BuildEvaluationAliases(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&aliases)) return *error;
    const auto alias_cost = std::get<EvaluationAliasCost>(aliases);
    admit(alias_cost.bytes, 1); step(alias_cost.work);
    std::optional<ArrayInitializationPlan> arrays;
    std::vector<std::optional<analysis::OpId>> literal_projection;
    auto projected_literals = std::span<const std::optional<analysis::OpId>>(plan.class_projection.owners);
    if (has_spread) {
      auto built = BuildArrayInitializations(graph, array_source.contract, array_source.constants, array_source.names,
          {bytes, work}, &plan);
      if (const auto* error = std::get_if<ArrayInitializationError>(&built)) {
        if (array_source.failure) *array_source.failure = *error;
        return *error == ArrayInitializationError::kResourceLimit ? Error::kResourceLimit :
            *error == ArrayInitializationError::kWorkLimit ? Error::kWorkLimit : Error::kUnsupportedOperation;
      }
      arrays = std::get<ArrayInitializationPlan>(std::move(built)); admit(arrays->allocated_bytes, 1); step(arrays->work);
      admit(graph.ops.size(), sizeof(std::optional<analysis::OpId>)); literal_projection.resize(graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        step(); literal_projection[i] = arrays->owners[i] ? arrays->owners[i] :
            (plan.class_projection.owners.empty() ? std::nullopt : plan.class_projection.owners[i]);
      }
      projected_literals = literal_projection;
    }
    if (has_definitions || has_spread) {
      auto built = flow == EvaluationFlow::kPhased ? BuildPhasedLiteralPlan(graph, plan, {bytes, work}, module_index.peers, projected_literals) :
          BuildLiteralPlan(graph, {bytes, work}, module_index.peers, projected_literals, plan.receiver_guards, plan.cells ? &*plan.cells : nullptr);
      const auto* error = std::get_if<LiteralError>(&built);
      if (error) {
        if (*error == LiteralError::kResourceLimit) return Error::kResourceLimit;
        if (*error == LiteralError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      plan.literals = std::get<LiteralPlan>(std::move(built));
      plan.literals->arrays = std::move(arrays);
      admit(plan.literals->allocated_bytes, 1); step(plan.literals->work);
      for (const auto& object : plan.literals->objects) {
        step();
        if (object.form == LiteralForm::kStatements) continue;
        if (flow == EvaluationFlow::kNormal) literal_allocations[object.allocation.value] = 1;
        else pinned[object.allocation.value] = 1;
      }
      for (const auto id : plan.literals->definitions) {
        step();
        // Native literal RHSs belong to the allocation's separate interval.
        // Staged definitions remain statement roots; common operand scheduling
        // owns their RHS order and snapshots rather than blanket pinning.
        if (IsStagedLiteral(&*plan.literals, id)) continue;
        const auto rhs = ResolveEvaluationOperand(plan, graph.uses[graph.ops[id.value].first_use.value + 1].operand);
        if (const auto* value = std::get_if<analysis::ValueId>(&rhs))
          if (const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin))
            pinned[result->op.value] = 1;
      }
    }
    if (const auto* arrays = ArrayInitializations(plan)) {
      for (std::size_t i = 0; i < arrays->nested.size(); ++i) {
        step(); if (arrays->nested[i]) plan.operations[i] = {EvaluationKind::kNestedLiteral, arrays->nested[i], {}};
      }
      for (const auto& array : arrays->arrays) {
        step(); literal_allocations[array.allocation.value] = 1; pinned[array.allocation.value] = 1;
      }
      for (const auto& item : arrays->steps) {
        step(); plan.operations[item.operation.value] = {item.value ? EvaluationKind::kInitializer : EvaluationKind::kArrayCursor, {}, {}};
        if (item.value) {
          const auto operand = ResolveEvaluationOperand(plan, graph.uses[item.value->value].operand);
          if (const auto* value = std::get_if<analysis::ValueId>(&operand))
            if (const auto* result = std::get_if<analysis::OpResult>(&graph.values[value->value].origin)) pinned[result->op.value] = 1;
        }
      }
    }
    if (plan.literals && !plan.literals->nested.empty()) {
      for (std::size_t i = 0; i < plan.literals->nested.size(); ++i) {
        step(); if (plan.literals->nested[i])
          plan.operations[i] = {EvaluationKind::kNestedLiteral, plan.literals->nested[i], {}};
      }
    }
    TreeCheckBudget use_budget({bytes,work});
    if (assignments) BuildAssignmentValues(graph, plan, use_budget);
    if (proposed) *proposed = !plan.assignment_values.empty();
    visible = IndexEvaluationUses(graph,plan,module_index,literal_allocations,plan.class_projection.owners,
        flow == EvaluationFlow::kNormal && plan.cells ? &*plan.cells : nullptr,plan.receiver_guards,use_budget);
    admit(use_budget.Bytes(),1); step(use_budget.Work());
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
        witness = EvaluationAt(graph, plan, {static_cast<std::uint32_t>(index)}, EvaluationObligation::kPlan);
        const auto& op = graph.ops[index];
        Require(op.block.value == b && op.order == order, Error::kInvalidGraph);
        if(op.life==analysis::ValueLife::kLive&&std::holds_alternative<analysis::ValueConcurrentNotification>(op.operation)) {
          Require(plan.concurrent&&plan.concurrent->members[index],Error::kOwnership);
          plan.operations[index]={EvaluationKind::kConcurrentNotification,{},{}};++serial;continue;
        }
        if (ForInOperation(plan, {static_cast<std::uint32_t>(index)})) {
          plan.operations[index] = {EvaluationKind::kForInEvent, {}, {}};
          // Ordered protocol boundary: never an elided effect or eager expression.
          ++serial; continue;
        }
        if (IsArrayInitialization(plan, {static_cast<std::uint32_t>(index)})) continue;
        if (op.life != analysis::ValueLife::kLive ||
            plan.operations[index].kind == EvaluationKind::kElidedPure ||
            plan.operations[index].kind == EvaluationKind::kAlias ||
            plan.operations[index].kind == EvaluationKind::kNestedLiteral) continue;
        if (std::binary_search(plan.receiver_guards.begin(), plan.receiver_guards.end(), analysis::OpId{static_cast<std::uint32_t>(index)})) {
          plan.operations[index].kind = EvaluationKind::kReceiverGuard;
          continue;
        }
        if (const auto peer = GuardedPeer(plan, {static_cast<std::uint32_t>(index)})) {
          plan.operations[index] = GuardedInput(plan, {static_cast<std::uint32_t>(index)})
              ? OpEvaluation{EvaluationKind::kRoot, {}, TemporaryReason::kCrossRegionValue}
              : OpEvaluation{EvaluationKind::kGuardedCellRead, {}, {}};
          ++serial;
          continue;
        }
        if (plan.cells &&
            IsInitializedCellGuard(graph, *plan.cells, analysis::OpId{static_cast<std::uint32_t>(index)})) {
          plan.operations[index].kind = EvaluationKind::kCellGuard;
          continue;
        }
        if (IsEnvironmentState(op.operation)) {
          Require(plan.cells.has_value(), Error::kUnsupportedOperation);
          plan.operations[index].kind = EvaluationKind::kEnvironment;
          // An unconsumable serial event prevents ordered subtrees (including
          // function creation) from fusing across a push/pop/current-env site.
          // The independent verifier checks creation's final root epoch too.
          ++serial;
          continue;
        }
        if (std::holds_alternative<analysis::ValueGuard>(op.operation) &&
            !module_index.peers.empty() && module_index.peers[index]) {
          plan.operations[index].kind = EvaluationKind::kModuleGuard;
          continue;
        }
        if (projected_op(index)) { plan.operations[index].kind = EvaluationKind::kClassProjection; continue; }
        const auto shape = DescribeEvaluation(op.operation);
        if (const auto error = CheckArgumentSnapshot(graph, op)) return *error;
        Require(shape.has_value(), Error::kUnsupportedOperation);
        Require(op.use_count == shape->Count() && shape->expression == op.result.has_value(),
                Error::kInvalidGraph);
        auto& placement = plan.operations[index];
        if (!IsReceiverField(graph, op) && !IsStagedLiteral(plan.literals ? &*plan.literals : nullptr, {static_cast<std::uint32_t>(index)}) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) {
          Require(plan.literals.has_value(), Error::kUnsupportedOperation);
          placement.kind = EvaluationKind::kInitializer;
          continue;
        }
        placement.kind = EvaluationKind::kRoot;
        const auto visible_uses = visible[index];
        if (visible_uses.first) {
          const auto use = *visible_uses.first;
          const auto& occurrence = graph.uses[use.value];
          if (visible_uses.multiple) placement.temporary = TemporaryReason::kMultipleUses;
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
          if (const auto error = CheckEvaluationUse(graph, occurrence, *shape, ordinal - 1)) return *error;
          const auto result = EvaluationProducer(graph, plan, use);
          if (!result || visible[result->value].first != use || visible[result->value].multiple) continue;
          if (pinned[result->value]) continue;
          // STOBJBYVALUE receives an already evaluated RHS. JS computes its
          // Reference (including key coercion) before evaluating RHS syntax.
          // Retain the producer snapshot, even for a pure parent of a getter.
          if (shape->NeedsSnapshot(occurrence.role)) continue;
          if (PredicateSnapshot(graph, op, occurrence.operand)) continue;
          const auto& producer = graph.ops[result->value];
          if (producer.block != op.block) continue;
          Require(producer.order < order, Error::kUnavailableValue);
          const auto interval = groups[result->value];
          if (interval.count != 0 && interval.end != expected) continue;
          Require(interval.count <= expected, Error::kInvalidGraph);
          expected -= interval.count;
          effects += interval.count;
          auto& child = plan.operations[result->value];
          witness = EvaluationAt(graph, plan, *result, EvaluationObligation::kProducerOwnership);
          witness.expected_consumer = use; witness.expected_owner = OperationLocation(graph, {static_cast<std::uint32_t>(index)});
          Require(child.kind == EvaluationKind::kRoot, Error::kOwnership);
          child = {EvaluationKind::kInline, use, {}};
        }
        if (OrderedEvaluation(op)) {
          ++serial; ++effects;
          // Only a paired read owns the second original guard event. An
          // initialized read remains one effect; no synthetic guard epoch.
          if (std::holds_alternative<analysis::ValueModuleRead>(op.operation) &&
              !module_index.peers.empty() && module_index.peers[index]) { ++serial; ++effects; }
        }
        groups[index] = {effects, effects ? serial : 0};
      }
      next_op += block.op_count;
    }
    witness = {};
    Require(next_op == graph.ops.size(), Error::kInvalidGraph);
    if (plan.phis) {
      // Every producer uses the same original-edge ownership proof, including
      // checked module reads. Their TDZ event and catch staging remain part of
      // the independently verified pair, not a producer-specific root pin.
      TreeCheckBudget phi_budget({bytes, work});
      const auto literal_ends = plan.literals ? IndexPhiLiteralEnds(graph, *plan.literals, phi_budget) :
          std::vector<std::optional<analysis::OpId>>{};
      const auto phi_inputs = IndexPhiExpressionInputs(graph, *plan.phis, module_index.peers, literal_ends, phi_budget, plan.aliases);
      admit(phi_budget.Bytes(), 1); step(phi_budget.Work());
      for (std::size_t i = 0; i < phi_inputs.size(); ++i) {
        step(); if (!phi_inputs[i] || pinned[i]) continue;
        auto& placement = plan.operations[i];
        if (placement.kind != EvaluationKind::kRoot) continue;
        placement = {EvaluationKind::kPhiInput, phi_inputs[i], {}};
      }
    }
    if (flow == EvaluationFlow::kNormal) {
      TreeCheckBudget logical_budget({bytes, work});
      // Use-site assignments are independent of phi/predicate syntax. A later
      // logical LHS proof may supersede the provisional snapshot using the
      // original forest, without losing any other retained observation.
      witness = {}; witness.obligation = EvaluationObligation::kProjection;
      BuildPredicateSnapshots(graph, plan, logical_budget);
      const auto inputs = FindLogicalLeftInputs(graph, plan, logical_budget);
      for (std::size_t i = 0; i < inputs.size(); ++i) {
        step(); if (inputs[i] && !pinned[i])
          plan.operations[i] = {EvaluationKind::kLogicalLeft, inputs[i], {}};
      }
      CommitPredicateSnapshots(graph, plan, logical_budget);
      admit(logical_budget.Bytes(), 1); step(logical_budget.Work());
    }
    if (flow == EvaluationFlow::kNormal && plan.phis) {
      TreeCheckBudget logical_budget({bytes, work});
      witness = {}; witness.obligation = EvaluationObligation::kProjection;
      BuildLogicalValues(graph, plan, logical_budget);
      admit(logical_budget.Bytes(), 1); step(logical_budget.Work());
    }
    if (flow == EvaluationFlow::kNormal) {
      // Closed pure values need no local storage merely because a block was
      // split or laid out backwards. Keep original single-use ownership and
      // the same source origin; this never moves a context read or effect.
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        step();
        if (pinned[i] || plan.operations[i].kind != EvaluationKind::kRoot ||
            !IsClosedPureValue(graph, {static_cast<std::uint32_t>(i)}) ||
            visible[i].multiple || !visible[i].first) continue;
        const auto use = *visible[i].first;
        const auto& occurrence = graph.uses[use.value];
        const auto* owner = std::get_if<analysis::OpId>(&occurrence.owner);
        if (!owner || graph.ops[owner->value].block == graph.ops[i].block) continue;
        const auto shape = DescribeEvaluation(graph.ops[owner->value].operation);
        if (!shape || shape->NeedsSnapshot(occurrence.role) ||
            PredicateSnapshot(graph, graph.ops[owner->value], occurrence.operand)) continue;
        if (plan.closed_expressions.empty()) {
          admit(graph.ops.size(), sizeof(std::optional<analysis::UseId>)); step(graph.ops.size());
          plan.closed_expressions.resize(graph.ops.size());
          admit(plan.closed_expressions.capacity()-graph.ops.size(), sizeof(std::optional<analysis::UseId>));
        }
        plan.closed_expressions[i] = use;
        plan.operations[i] = {EvaluationKind::kInline, use, {}};
      }
    }
    const auto expressions = BuildPhaseExpressions(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&expressions)) return *error;
    const auto& phase_cost = std::get<PhaseEvaluationCost>(expressions);
    admit(phase_cost.bytes, 1); step(phase_cost.work);
    if (flow == EvaluationFlow::kPhased) {
      TreeCheckBudget selection_budget({bytes, work});
      BuildPhaseSelectionOperands(graph, plan, selection_budget);
      BuildPhasedObjectOperands(graph, plan, selection_budget);
      witness = {}; witness.obligation = EvaluationObligation::kProjection;
      ComposePhaseExpressions(graph, plan, selection_budget);
      witness = {}; witness.obligation = EvaluationObligation::kProjection;
      ComposeCheckedCalls(graph, plan, selection_budget);
      admit(selection_budget.Bytes(), 1); step(selection_budget.Work());
    }
    const auto normalized = BuildIntegerNaNs(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<EvaluationFailure>(&normalized)) return *error;
    const auto normalization_cost = std::get<IntegerNaNCost>(normalized);
    admit(normalization_cost.bytes, 1); step(normalization_cost.work);
    const auto checked = VerifyEvaluationForFlow(graph, plan, {bytes, work}, flow);
    if (const auto* error = std::get_if<EvaluationFailure>(&checked)) return *error;
    step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = plan.checked_failures.capacity() * sizeof(GuardedCallFailure) + plan.assignment_values.capacity() * sizeof(std::optional<analysis::OpId>) + plan.assignment_consumers.capacity() * sizeof(std::optional<analysis::UseId>) + plan.phase_children.capacity() * sizeof(std::optional<analysis::OpId>) + plan.closed_expressions.capacity() * sizeof(std::optional<analysis::UseId>) + plan.phase_references.capacity() * sizeof(std::optional<analysis::UseId>) + plan.phase_prefixes.capacity() * sizeof(std::optional<analysis::OpId>) + (plan.phase_compositions.capacity() + plan.checked_compositions.capacity()) * sizeof(analysis::OpId) + plan.phase_transports.capacity() * sizeof(std::optional<analysis::UseId>) + plan.phase_selections.capacity() * sizeof(PhaseSelectionOperand) + plan.integer_nans.capacity() * sizeof(NaNProjection) + plan.operations.capacity() * sizeof(OpEvaluation) +
        plan.logical_values.capacity() * sizeof(std::optional<analysis::OpId>) +
        plan.logical_objects.capacity() * sizeof(std::optional<analysis::OpId>) +
        plan.aliases.capacity() * sizeof(analysis::ValueId) +
        plan.functions.capacity() * sizeof(FunctionBinding) +
        plan.modules.capacity() * sizeof(ModuleEvaluation) +
        plan.class_initializers.capacity() * sizeof(ClassInitializerCall) + plan.class_projection.allocated_bytes +
        plan.class_definitions.capacity() * sizeof(ClassDefinitionSite) +
        plan.class_prototype_reads.capacity() * sizeof(ClassPrototypeRead) +
        plan.receiver_guards.capacity() * sizeof(analysis::OpId) +
        (plan.phis ? plan.phis->allocated_bytes : 0) +
        (plan.literals ? plan.literals->allocated_bytes : 0) +
        (ArrayInitializations(plan) ? ArrayInitializations(plan)->allocated_bytes : 0);
    for (const auto& call : plan.phase_selections) plan.allocated_bytes += call.prefix.capacity() * sizeof(analysis::OpId) + call.ancestors.capacity() * sizeof(PhaseCallReference);
    if (plan.for_in) plan.allocated_bytes += plan.for_in->allocated_bytes + plan.for_in_events->allocated_bytes;
    if (plan.self_order) plan.allocated_bytes += plan.self_order->AllocatedBytes();
    if (plan.cells) plan.allocated_bytes += plan.cells->allocated_bytes;
    if (plan.exceptions) plan.allocated_bytes += plan.exceptions->allocated_bytes;
    for (const auto& function : plan.functions) {
      step(); plan.allocated_bytes += function.captures.capacity() * sizeof(CellId);
    }
    plan.work = budget.work - work;
    return plan;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const EvaluationFailure& error) { return error;
  } catch (Error error) { return EvaluationFailure{error, witness};
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<EvaluationPlan, EvaluationFailure> BuildEvaluation(const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules,
    EvaluationFlow flow, std::span<const CellDemand> demands, std::span<const ClassInitializerCall> initializers,
    analysis::ClosureWrites closure_writes, std::span<const ClassDefinitionSite> definitions,
    std::span<const analysis::OpId> receiver_guards, std::span<const IncomingCell> entry_demands,
    const ClassSelfOrder* self_order, std::span<const ClassPrototypeRead> prototype_reads, ArraySourceInput array_source, std::span<const analysis::GuardedRead> guarded = {}) {
  // Reserve two disjoint work allowances before trying an optional projection.
  // A failed subpass need not report partial work: its entire allowance remains
  // charged. The byte allowance is reused only after the failed plan is gone.
  std::uint64_t scan = 0;
  bool has_write = false, has_call = false;
  for (const auto& op : graph.ops) {
    if (scan == budget.work) return Error::kWorkLimit;
    ++scan;
    const auto shape = DescribeEvaluation(op.operation);
    has_write |= op.life == analysis::ValueLife::kLive && shape && shape->assignment_result.has_value();
    const auto* call = std::get_if<analysis::ValueInvoke>(&op.operation);
    has_call |= op.life == analysis::ValueLife::kLive && call && call->arguments != 0;
  }
  const bool attempt = has_write && has_call;
  const auto allowance = attempt ? (budget.work - scan) / 2 : budget.work - scan;
  bool proposed = false;
  auto result = BuildEvaluationAttempt(graph, {budget.bytes, allowance}, incoming, functions, modules, flow, demands, initializers, closure_writes, definitions, receiver_guards, entry_demands, self_order, prototype_reads, array_source, attempt, &proposed, guarded);
  if (auto* plan = std::get_if<EvaluationPlan>(&result)) {
    // Twice actual work also admits this same successful attempt at plan.work.
    plan->work = scan + plan->work * (attempt ? 2 : 1);
    return result;
  }
  const auto error = std::get<EvaluationFailure>(result);
  if (!proposed || error == Error::kResourceLimit || error == Error::kWorkLimit) return result;
  auto fallback = BuildEvaluationAttempt(graph, {budget.bytes, allowance}, incoming, functions, modules, flow, demands, initializers, closure_writes, definitions, receiver_guards, entry_demands, self_order, prototype_reads, array_source, false, nullptr, guarded);
  if (auto* plan = std::get_if<EvaluationPlan>(&fallback)) plan->work += scan + allowance;
  return fallback;
}
}  // namespace
std::variant<EvaluationPlan, EvaluationFailure> BuildEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules, std::span<const CellDemand> demands,
    std::span<const ClassInitializerCall> initializers, analysis::ClosureWrites closure_writes, std::span<const ClassDefinitionSite> definitions, std::span<const analysis::OpId> receiver_guards, std::span<const IncomingCell> entry_demands, const ClassSelfOrder* self_order, std::span<const ClassPrototypeRead> prototype_reads, ArraySourceInput arrays) {
  return BuildEvaluation(graph, budget, incoming, functions, modules, EvaluationFlow::kNormal, demands, initializers, closure_writes, definitions, receiver_guards, entry_demands, self_order, prototype_reads, arrays);
}
std::variant<EvaluationPlan, EvaluationFailure> BuildPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules, std::span<const CellDemand> demands,
    std::span<const ClassInitializerCall> initializers, analysis::ClosureWrites closure_writes, ArraySourceInput arrays, std::span<const analysis::GuardedRead> guarded) {
  return BuildEvaluation(graph, budget, incoming, functions, modules, EvaluationFlow::kPhased, demands, initializers, closure_writes, {}, {}, {}, nullptr, {}, arrays, guarded);
}
}  // namespace abcd::source
