#include "evaluation_plan.h"
#include "evaluation_flow.h"
#include "evaluation_liveness.h"
#include "evaluation_aliases.h"
#include "module_evaluation.h"
#include "phase_evaluation.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct EffectInterval { std::uint32_t count = 0; std::uint32_t end = 0; };
std::variant<EvaluationPlan, EvaluationError> BuildEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules,
    EvaluationFlow flow, std::span<const CellDemand> demands, std::span<const ClassInitializerCall> initializers,
    analysis::ClosureWrites closure_writes, std::span<const ClassDefinitionSite> definitions) {
  try {
    Require(incoming == IncomingCells::kReject || incoming == IncomingCells::kConditional, Error::kUnsupportedOperation);
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
    const auto availability = CheckEvaluationFlow(graph, flow, {bytes, work});
    if (const auto* error = std::get_if<Error>(&availability)) return *error;
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
    plan.closure_writes = closure_writes;
    plan.arguments = graph.arguments;
    auto projected = ProjectClassMembers(graph, initializers, {bytes, work}, definitions);
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
    if (const auto* error = std::get_if<EvaluationError>(&indexed_modules)) return *error;
    auto module_index = std::get<ModuleEvaluationIndex>(std::move(indexed_modules));
    admit(module_index.allocated_bytes, 1); step(module_index.work);
    admit(graph.ops.size(), sizeof(std::uint8_t)); step(graph.ops.size());
    std::vector<std::uint8_t> pinned(graph.ops.size());
    admit(pinned.capacity() - graph.ops.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::uint8_t) + sizeof(EvaluationUses));
    step(graph.ops.size()); step(graph.ops.size());
    std::vector<std::uint8_t> literal_allocations(graph.ops.size());
    std::vector<EvaluationUses> visible(graph.ops.size());
    admit(literal_allocations.capacity() - graph.ops.size(), sizeof(std::uint8_t));
    admit(visible.capacity() - graph.ops.size(), sizeof(EvaluationUses));
    bool has_definitions = false;
    bool has_cells = !demands.empty();
    bool has_arguments = false;
    std::size_t next_function = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      has_arguments |= op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueArgumentSnapshot>(op.operation);
      step(); has_definitions |= !projected_op(i) && op.life == analysis::ValueLife::kLive &&
          (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation));
      has_cells |= op.life == analysis::ValueLife::kLive && NeedsCellEvaluation(op.operation);
      if (op.life == analysis::ValueLife::kLive && !projected_op(i) && std::holds_alternative<analysis::ValueFunctionCreation>(op.operation)) {
        Require(next_function < functions.size() && functions[next_function++].creation.value == i, Error::kOwnership);
        // Creation is ordered, not intrinsically a statement. Environment
        // fences below prevent its subtree from crossing implicit scope state.
      }
      // Normal-flow guarded reads may fuse only through the ordinary ordered
      // suffix algorithm; the verifier still consumes both read and guard.
      // Intervening effects and multiple uses retain the original snapshot.
      if (flow == EvaluationFlow::kPhased && op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueModuleRead>(op.operation)) pinned[i] = 1;
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
      const auto build_cells = flow == EvaluationFlow::kPhased ? BuildPhasedCellEvaluation : BuildCellEvaluation;
      auto built = build_cells(graph, {bytes, work}, incoming,
          functions.empty() ? closure_writes : analysis::ClosureWrites::kLanguage, demands);
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
          : BuildPhasedPhiCopies(graph, {bytes, work}, plan.holes ? PhiDomain::kHoleTransport : PhiDomain::kLanguage);
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
    std::vector<EffectInterval> groups(graph.ops.size());
    Require(plan.operations.capacity() <= graph.ops.size() && groups.capacity() <= graph.ops.size(),
            Error::kResourceLimit);
    if (flow == EvaluationFlow::kPhased) {
      // Normal inputs already use verified IR simplification. Phase inputs
      // keep exact import correspondence; elide only in the source projection.
      const auto live = ElidePureEvaluations(graph, plan, {bytes, work});
      if (const auto* error = std::get_if<Error>(&live)) return *error;
      const auto cost = std::get<EvaluationLivenessCost>(live);
      admit(cost.bytes, 1); step(cost.work);
      const auto aliases = BuildEvaluationAliases(graph, plan, {bytes, work});
      if (const auto* error = std::get_if<Error>(&aliases)) return *error;
      const auto alias_cost = std::get<EvaluationAliasCost>(aliases);
      admit(alias_cost.bytes, 1); step(alias_cost.work);
    }
    if (has_definitions) {
      auto built = flow == EvaluationFlow::kPhased ? BuildPhasedLiteralPlan(graph, plan, {bytes, work}) :
          BuildLiteralPlan(graph, {bytes, work}, module_index.peers, plan.class_projection.owners);
      const auto* error = std::get_if<LiteralError>(&built);
      if (error) {
        if (*error == LiteralError::kResourceLimit) return Error::kResourceLimit;
        if (*error == LiteralError::kWorkLimit) return Error::kWorkLimit;
        return Error::kUnsupportedOperation;
      }
      plan.literals = std::get<LiteralPlan>(std::move(built));
      admit(plan.literals->allocated_bytes, 1); step(plan.literals->work);
      for (const auto& object : plan.literals->objects) {
        step();
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
    if (plan.literals && !plan.literals->nested.empty()) {
      for (std::size_t i = 0; i < plan.literals->nested.size(); ++i) {
        step(); if (plan.literals->nested[i])
          plan.operations[i] = {EvaluationKind::kNestedLiteral, plan.literals->nested[i], {}};
      }
    }
    // Producer use chains are disjoint. Charge their total once, and cache the
    // projection so even a shared aggregate with many consumers stays linear.
    step(graph.uses.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      if (graph.ops[i].life == analysis::ValueLife::kLive)
        visible[i] = ProjectEvaluationUses(graph, {static_cast<std::uint32_t>(i)}, module_index,
            literal_allocations[i] != 0, plan.class_projection.owners);
    }
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
        if (op.life != analysis::ValueLife::kLive ||
            plan.operations[index].kind == EvaluationKind::kElidedPure ||
            plan.operations[index].kind == EvaluationKind::kAlias ||
            plan.operations[index].kind == EvaluationKind::kNestedLiteral) continue;
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
        if (!IsReceiverField(graph, op) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) {
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
          const auto* value = std::get_if<analysis::ValueId>(&occurrence.operand);
          if (!value) continue;
          const auto& definition = graph.values[value->value];
          const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
          if (!result || visible[result->op.value].first != use || visible[result->op.value].multiple) continue;
          if (pinned[result->op.value]) continue;
          // STOBJBYVALUE receives an already evaluated RHS. JS computes its
          // Reference (including key coercion) before evaluating RHS syntax.
          // Retain the producer snapshot, even for a pure parent of a getter.
          if (shape->NeedsSnapshot(occurrence.role)) continue;
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
        if (OrderedEvaluation(op)) {
          ++serial; ++effects;
          // The pinned read executes both original ordered events. The guard
          // has no independent source evaluation, but is never erased from IR.
          if (std::holds_alternative<analysis::ValueModuleRead>(op.operation)) { ++serial; ++effects; }
        }
        groups[index] = {effects, effects ? serial : 0};
      }
      next_op += block.op_count;
    }
    Require(next_op == graph.ops.size(), Error::kInvalidGraph);
    const auto expressions = BuildPhaseExpressions(graph, plan, {bytes, work});
    if (const auto* error = std::get_if<Error>(&expressions)) return *error;
    const auto& phase_cost = std::get<PhaseEvaluationCost>(expressions);
    admit(phase_cost.bytes, 1); step(phase_cost.work);
    const auto checked = VerifyEvaluationForFlow(graph, plan, {bytes, work}, flow);
    if (const auto* error = std::get_if<EvaluationError>(&checked)) return *error;
    step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = plan.operations.capacity() * sizeof(OpEvaluation) +
        plan.aliases.capacity() * sizeof(analysis::ValueId) +
        plan.functions.capacity() * sizeof(FunctionBinding) +
        plan.modules.capacity() * sizeof(ModuleEvaluation) +
        plan.class_initializers.capacity() * sizeof(ClassInitializerCall) + plan.class_projection.allocated_bytes +
        plan.class_definitions.capacity() * sizeof(ClassDefinitionSite) +
        (plan.phis ? plan.phis->allocated_bytes : 0) +
        (plan.literals ? plan.literals->allocated_bytes : 0);
    if (plan.cells) plan.allocated_bytes += plan.cells->allocated_bytes;
    if (plan.exceptions) plan.allocated_bytes += plan.exceptions->allocated_bytes;
    for (const auto& function : plan.functions) {
      step(); plan.allocated_bytes += function.captures.capacity() * sizeof(CellId);
    }
    plan.work = budget.work - work;
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<EvaluationPlan, EvaluationError> BuildEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules, std::span<const CellDemand> demands,
    std::span<const ClassInitializerCall> initializers, analysis::ClosureWrites closure_writes, std::span<const ClassDefinitionSite> definitions) {
  return BuildEvaluation(graph, budget, incoming, functions, modules, EvaluationFlow::kNormal, demands, initializers, closure_writes, definitions);
}
std::variant<EvaluationPlan, EvaluationError> BuildPhasedEvaluationPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    std::span<const FunctionBinding> functions, std::span<const ModuleEvaluation> modules, std::span<const CellDemand> demands,
    std::span<const ClassInitializerCall> initializers) {
  return BuildEvaluation(graph, budget, incoming, functions, modules, EvaluationFlow::kPhased, demands, initializers, analysis::ClosureWrites::kUnknown, {});
}
}  // namespace abcd::source
