#include "cell_evaluation.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = CellEvaluationError;
enum class CellFlow { kNormal, kPhased };
template <class T, class E> T Take(std::variant<T, E> result, Error failure) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  const auto error = std::get<E>(result);
  if (error == E::kResourceLimit) throw Error::kResourceLimit;
  if (error == E::kWorkLimit) throw Error::kWorkLimit;
  throw failure;
}
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : initial_(budget), left_(budget) {}
  void Step(std::uint64_t n) { if (n > left_.work) throw Error::kWorkLimit; left_.work -= n; }
  void Admit(std::size_t bytes) { if (bytes > left_.bytes) throw Error::kResourceLimit; left_.bytes -= bytes; }
  analysis::SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  analysis::SemanticBudget initial_, left_;
};
void Identities(const analysis::ValueProgram& graph, const CellPlan& cells, IncomingCells incoming, Quota& quota) {
  switch (incoming) {
    case IncomingCells::kReject: case IncomingCells::kConditional: break;
    default: throw Error::kInitialization;
  }
  for (const auto& cell : cells.cells) {
    quota.Step(1);
    if (std::holds_alternative<IncomingCell>(cell.storage) && incoming == IncomingCells::kReject)
      throw Error::kInitialization;
  }
  for (const auto& op : graph.ops) {
    quota.Step(1);
    if (op.life != analysis::ValueLife::kLive || !IsEnvironmentState(op.operation)) continue;
    const bool result = !std::holds_alternative<analysis::ValueEnvironmentPop>(op.operation);
    if (op.use_count != 0 || op.result.has_value() != result) throw Error::kInternalIdentity;
    if (op.result && (graph.values[op.result->value].first_use ||
        graph.values[op.result->value].domain != analysis::ValueKinds::Environment()))
      throw Error::kInternalIdentity;
  }
}
}  // namespace
bool IsEnvironmentState(const analysis::ValueOperation& operation) {
  return std::holds_alternative<analysis::ValueEnvironmentPush>(operation) ||
      std::holds_alternative<analysis::ValueEnvironmentPop>(operation) ||
      std::holds_alternative<analysis::ValueCurrentEnvironment>(operation);
}
bool NeedsCellEvaluation(const analysis::ValueOperation& operation) {
  return IsEnvironmentState(operation) || std::holds_alternative<analysis::ValueLexicalLoad>(operation) ||
      std::holds_alternative<analysis::ValueLexicalStore>(operation);
}
namespace {
std::variant<std::uint64_t, CellEvaluationError> VerifyEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget,
    CellFlow flow) {
  try {
    Quota quota(budget);
    const auto build_environments = flow == CellFlow::kPhased
        ? analysis::BuildPhasedValueEnvironments : analysis::BuildValueEnvironments;
    auto environments = Take(build_environments(graph, quota.Remaining()), Error::kEnvironment);
    quota.Admit(environments.allocated_bytes); quota.Step(environments.work);
    const auto verify_initialization = flow == CellFlow::kPhased
        ? VerifyPhasedCellInitializations : VerifyCellInitializations;
    quota.Step(Take(verify_initialization(graph, environments, plan.cells,
        plan.initialization.closure_writes, plan.initialization, quota.Remaining()), Error::kInitialization));
    Identities(graph, plan.cells, plan.incoming, quota);
    return quota.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<CellEvaluation, CellEvaluationError> BuildEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    analysis::ClosureWrites writes, CellFlow flow, std::span<const CellDemand> demands) {
  try {
    Quota quota(budget); quota.Admit(sizeof(CellEvaluation));
    const auto build_environments = flow == CellFlow::kPhased
        ? analysis::BuildPhasedValueEnvironments : analysis::BuildValueEnvironments;
    auto environments = Take(build_environments(graph, quota.Remaining()), Error::kEnvironment);
    quota.Admit(environments.allocated_bytes); quota.Step(environments.work);
    const auto build_cells = flow == CellFlow::kPhased ? BuildDemandedPhasedCellPlan : BuildDemandedCellPlan;
    auto cells = Take(build_cells(graph, environments, demands, quota.Remaining()), Error::kCells);
    quota.Admit(cells.allocated_bytes); quota.Step(cells.work);
    const auto build_initialization = flow == CellFlow::kPhased
        ? BuildPhasedCellInitializations : BuildCellInitializations;
    auto initialization = Take(build_initialization(graph, environments, cells,
        writes, quota.Remaining()), Error::kInitialization);
    quota.Admit(initialization.allocated_bytes); quota.Step(initialization.work);
    Identities(graph, cells, incoming, quota);
    const auto retained = sizeof(CellEvaluation) + cells.allocated_bytes + initialization.allocated_bytes;
    return CellEvaluation{std::move(cells), std::move(initialization), incoming, retained, quota.Work()};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<CellEvaluation, CellEvaluationError> BuildCellEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    analysis::ClosureWrites writes, std::span<const CellDemand> demands) {
  return BuildEvaluation(graph, budget, incoming, writes, CellFlow::kNormal, demands);
}
std::variant<std::uint64_t, CellEvaluationError> VerifyCellEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget) {
  return VerifyEvaluation(graph, plan, budget, CellFlow::kNormal);
}
std::variant<CellEvaluation, CellEvaluationError> BuildPhasedCellEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    analysis::ClosureWrites writes, std::span<const CellDemand> demands) {
  return BuildEvaluation(graph, budget, incoming, writes, CellFlow::kPhased, demands);
}
std::variant<std::uint64_t, CellEvaluationError> VerifyPhasedCellEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget) {
  return VerifyEvaluation(graph, plan, budget, CellFlow::kPhased);
}
}  // namespace abcd::source
