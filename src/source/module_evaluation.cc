#include "module_evaluation.h"
#include "assignment_values.h"
#include "phase_evaluation.h"
#include "evaluation_aliases.h"
#include "tree_checks.h"
#include <bit>

#include <new>
#include <stdexcept>

namespace abcd::source {
std::vector<EvaluationUses> IndexEvaluationUses(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, const ModuleEvaluationIndex& modules,
    std::span<const std::uint8_t> literal_allocations,
    std::span<const std::optional<analysis::OpId>> class_projection,
    const CellEvaluation* initialized_cells, std::span<const analysis::OpId> receiver_guards,
    TreeCheckBudget& budget) {
  using namespace analysis;
  std::vector<EvaluationUses> result; budget.Array(result,graph.ops.size());
  for (std::size_t i=0;i<graph.uses.size();++i) {
    budget.Step(); const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || IsIdentityPhiInput(plan,use)) continue;
    const auto producer = EvaluationProducer(graph, plan, UseId{static_cast<std::uint32_t>(i)});
    if (!producer) continue;
    const auto id = *producer;
    if (const auto* owner = std::get_if<OpId>(&use.owner)) {
      if (IsArrayInitialization(plan, *owner) && use.role != OperandRole::kSource && use.role != OperandRole::kStoredValue) continue;
      if (plan.operations[owner->value].kind == EvaluationKind::kAlias ||
          plan.operations[owner->value].kind == EvaluationKind::kElidedPure) continue;
      if (!modules.peers.empty() && modules.peers[id.value] == *owner &&
          std::holds_alternative<ValueModuleRead>(graph.ops[id.value].operation)) continue;
      if (!class_projection.empty() && class_projection[owner->value]) continue;
      if (initialized_cells && IsInitializedCellGuard(graph,*initialized_cells,*owner)) continue;
      if (!receiver_guards.empty()) {
        budget.Step(std::bit_width(receiver_guards.size()));
        if (std::binary_search(receiver_guards.begin(),receiver_guards.end(),*owner)) continue;
      }
      if (literal_allocations[id.value] && use.role == OperandRole::kBase) {
        const auto& operation = graph.ops[owner->value].operation;
        if (std::holds_alternative<ValueOwnWrite>(operation) || std::holds_alternative<ValueIndexedWrite>(operation)) continue;
      }
    }
    if (IsMemberReceiver(graph,plan,use)) continue;
    auto& occurrence = result[id.value];
    if (occurrence.first) occurrence.multiple = true;
    else occurrence.first = UseId{static_cast<std::uint32_t>(i)};
  }
  return result;
}
std::variant<ModuleEvaluationIndex, EvaluationError> IndexModuleEvaluations(
    const analysis::ValueProgram& graph, std::span<const ModuleEvaluation> modules,
    analysis::SemanticBudget budget) {
  using Error = EvaluationError;
  try {
    const auto require = [](bool condition, Error error) { if (!condition) throw error; };
    auto work = budget.work;
    const auto step = [&] { require(work != 0, Error::kWorkLimit); --work; };
    ModuleEvaluationIndex result;
    // Avoid an I-sized empty table for the overwhelmingly common no-module case.
    if (!modules.empty()) {
      require(graph.ops.size() <= budget.bytes / sizeof(result.peers[0]), Error::kResourceLimit);
      require(graph.ops.size() <= work, Error::kWorkLimit);
      work -= graph.ops.size();
      result.peers.resize(graph.ops.size());
      require(result.peers.capacity() <= budget.bytes / sizeof(result.peers[0]), Error::kResourceLimit);
      result.allocated_bytes = result.peers.capacity() * sizeof(result.peers[0]);
    }
    std::size_t next = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      const auto& op = graph.ops[i];
      const auto* read = std::get_if<analysis::ValueModuleRead>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !read) continue;
      require(next < modules.size(), Error::kUnsupportedOperation);
      const auto& entry = modules[next++];
      require(entry.pair.read.value == i && entry.read == *read &&
          op.use_count == 0 && op.result && op.result->value < graph.values.size(), Error::kOwnership);
      if (!entry.pair.guard) {
        const auto* definition = std::get_if<analysis::OpResult>(&graph.values[op.result->value].origin);
        const bool normal = graph.phases.empty() && definition && !definition->commit;
        const bool phased = !graph.phases.empty() && definition && definition->commit &&
            definition->commit->value < graph.phases.size() &&
            graph.phases[definition->commit->value].phase == analysis::ExceptionPhase::kCommit &&
            graph.phases[definition->commit->value].operation == entry.pair.read;
        require((normal || phased) && read->space == analysis::ModuleSpace::kLocal &&
            definition && definition->op == entry.pair.read &&
            graph.values[op.result->value].life == analysis::ValueLife::kLive &&
            graph.values[op.result->value].domain.IsLanguage() &&
            op.normal == analysis::ModuleReadEffects(read->loading) && op.before_throw == op.normal &&
            op.exceptional == analysis::ExceptionalCompletion::kMayThrow, Error::kOwnership);
        continue;
      }
      require(std::uint64_t{i} + 1 == entry.pair.guard->value &&
          entry.pair.guard->value < graph.ops.size(), Error::kOwnership);
      const auto& guard = graph.ops[entry.pair.guard->value];
      const auto* check = std::get_if<analysis::ValueGuard>(&guard.operation);
      require(guard.life == analysis::ValueLife::kLive && check && check->name && !guard.result &&
          guard.use_count == 1 && guard.first_use.value < graph.uses.size() &&
          op.use_count == 0 && op.result && op.result->value < graph.values.size(), Error::kInvalidGraph);
      const auto* definition = std::get_if<analysis::OpResult>(&graph.values[op.result->value].origin);
      require(definition && definition->op == entry.pair.read &&
          graph.values[op.result->value].life == analysis::ValueLife::kLive, Error::kInvalidGraph);
      const auto& use = graph.uses[guard.first_use.value];
      require(std::get_if<analysis::OpId>(&use.owner) && std::get<analysis::OpId>(use.owner) == entry.pair.guard &&
          use.role == analysis::OperandRole::kGuardValue && use.purpose == analysis::ReadPurpose::kOperand &&
          std::get_if<analysis::ValueId>(&use.operand) && std::get<analysis::ValueId>(use.operand) == *op.result,
          Error::kOwnership);
      if (graph.phases.empty())
        require(op.block == guard.block && std::uint64_t{op.order} + 1 == guard.order, Error::kOrder);
      // Exact imported effects. Original input correspondence is a separate
      // prerequisite; malformed effect tables cannot erase the second event.
      require(op.normal == analysis::ModuleReadEffects(read->loading) && op.before_throw == op.normal &&
          guard.normal == analysis::LanguageEffect::kGuard && guard.before_throw == guard.normal &&
          op.exceptional == analysis::ExceptionalCompletion::kMayThrow &&
          guard.exceptional == analysis::ExceptionalCompletion::kMayThrow, Error::kOrder);
      result.peers[i] = entry.pair.guard;
      result.peers[entry.pair.guard->value] = entry.pair.read;
    }
    require(next == modules.size(), Error::kOwnership);
    if (!graph.phases.empty() && !modules.empty()) {
      const auto checked = VerifyModulePhaseOrder(graph, modules,
          {budget.bytes - result.allocated_bytes, work});
      if (const auto* failure = std::get_if<Error>(&checked)) return *failure;
      work -= std::get<std::uint64_t>(checked);
    }
    result.work = budget.work - work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
