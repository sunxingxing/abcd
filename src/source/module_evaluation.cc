#include "module_evaluation.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
EvaluationUses ProjectEvaluationUses(const analysis::ValueProgram& graph,
    analysis::OpId operation, const ModuleEvaluationIndex& modules, bool project_initializers,
    std::span<const std::optional<analysis::OpId>> class_projection) {
  const auto& op = graph.ops[operation.value];
  if (!op.result) return {};
  std::optional<analysis::UseId> consumed;
  if (std::holds_alternative<analysis::ValueModuleRead>(op.operation) && !modules.peers.empty())
    if (const auto guard = modules.peers[operation.value]) consumed = graph.ops[guard->value].first_use;
  EvaluationUses result;
  for (auto id = graph.values[op.result->value].first_use; id; id = graph.uses[id->value].next) {
    if (id == consumed) continue;
    const auto& use = graph.uses[id->value];
    if (const auto* owner = std::get_if<analysis::OpId>(&use.owner))
      if (!class_projection.empty() && class_projection[owner->value]) continue;
    if (project_initializers && use.role == analysis::OperandRole::kBase) {
      if (const auto* owner = std::get_if<analysis::OpId>(&use.owner)) {
        const auto& operation = graph.ops[owner->value].operation;
        if (std::holds_alternative<analysis::ValueOwnWrite>(operation) ||
            std::holds_alternative<analysis::ValueIndexedWrite>(operation)) continue;
      }
    }
    if (result.first) { result.multiple = true; break; }
    result.first = id;
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
          std::uint64_t{i} + 1 == entry.pair.guard.value && entry.pair.guard.value < graph.ops.size(), Error::kOwnership);
      const auto& guard = graph.ops[entry.pair.guard.value];
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
      require(op.normal == analysis::LanguageEffect::kModuleRead && op.before_throw == op.normal &&
          guard.normal == analysis::LanguageEffect::kGuard && guard.before_throw == guard.normal &&
          op.exceptional == analysis::ExceptionalCompletion::kMayThrow &&
          guard.exceptional == analysis::ExceptionalCompletion::kMayThrow, Error::kOrder);
      result.peers[i] = entry.pair.guard;
      result.peers[entry.pair.guard.value] = entry.pair.read;
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
