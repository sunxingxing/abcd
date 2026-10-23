#include "cell_evaluation.h"
#include "evaluation_input_scope.h"
#include "recovery_scratch.h"
#include "../analysis/value_flow.h"

#include <algorithm>
#include <bit>
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
void Identities(const analysis::ValueProgram& graph, const CellPlan& cells, IncomingCells incoming, Quota& quota, EvaluationInputSite* failure = nullptr) {
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
    EvaluationInputScope site(failure, {EvaluationInputCheck::kCellIdentity,
        analysis::OpId{static_cast<std::uint32_t>(&op - graph.ops.data())}, {}, op.result});
    const bool result = !std::holds_alternative<analysis::ValueEnvironmentPop>(op.operation);
    if (op.use_count != 0 || op.result.has_value() != result) throw Error::kInternalIdentity;
    if (op.result && (graph.values[op.result->value].first_use ||
        graph.values[op.result->value].domain != analysis::ValueKinds::Environment()))
      throw Error::kInternalIdentity;
  }
}
}  // namespace
std::optional<analysis::ValueId> InitializedCellGuardInput(const analysis::ValueProgram& graph, const CellEvaluation& cells, analysis::OpId id) {
  if (id.value >= graph.ops.size() || cells.initialization.revision != graph.revision ||
      cells.initialization.cfg_revision != graph.cfg_revision) return {};
  const auto& guard = graph.ops[id.value];
  if (guard.life != analysis::ValueLife::kLive || !std::holds_alternative<analysis::ValueGuard>(guard.operation) ||
      guard.result || guard.use_count != 1 || guard.first_use.value >= graph.uses.size()) return {};
  const auto& use = graph.uses[guard.first_use.value];
  const auto* owner = std::get_if<analysis::OpId>(&use.owner);
  const auto* value = std::get_if<analysis::ValueId>(&use.operand);
  if (!owner || *owner != id || use.life != analysis::ValueLife::kLive ||
      use.role != analysis::OperandRole::kGuardValue || use.purpose != analysis::ReadPurpose::kOperand ||
      !value || value->value >= graph.values.size()||cells.identities.canonical.size()!=graph.values.size()) return {};
  const auto root=cells.identities.canonical[value->value];
  if(root.value>=graph.values.size()||graph.values[value->value].life!=analysis::ValueLife::kLive||
      !graph.values[value->value].domain.IsLanguage())return {};
  const auto& definition = graph.values[root.value];
  const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
  if (!result || definition.life != analysis::ValueLife::kLive || !definition.domain.IsLanguage() ||
      result->op.value >= graph.ops.size() || result->op.value >= cells.initialization.reads.size()) return {};
  const auto& load = graph.ops[result->op.value];
  if (load.life != analysis::ValueLife::kLive || load.result != root ||
      !std::holds_alternative<analysis::ValueLexicalLoad>(load.operation)) return {};
  const auto& witness = cells.initialization.reads[result->op.value];
  if(std::holds_alternative<StoredInitialization>(witness) ||
      (cells.incoming == IncomingCells::kConditional && std::holds_alternative<IncomingInitialization>(witness)))return root;
  return {};
}
namespace {
struct DeferredActivation {
  analysis::SemanticBudget budget;
  std::optional<analysis::DominatorTree> flow;
  std::vector<std::optional<analysis::WorkBlockId>> commits;
  bool Before(const analysis::ValueProgram& graph, analysis::OpId allocation,
      analysis::OpId store, std::uint64_t& work) {
    if (!flow) {
      if (work > budget.work) return false;
      if (graph.phases.empty()) {
        auto checked = analysis::VerifyValueFlow(graph, {budget.bytes, budget.work - work});
        auto* proof = std::get_if<analysis::ValueFlowProof>(&checked);
        if (!proof) return false;
        work += proof->work; flow.emplace(std::move(proof->dominators));
      } else {
        if (graph.ops.size() > budget.bytes / sizeof(decltype(commits)::value_type)) return false;
        commits.resize(graph.ops.size());
        const auto bytes = commits.capacity() * sizeof(decltype(commits)::value_type);
        if (bytes > budget.bytes) return false;
        auto checked = analysis::VerifyPhasedValueFlow(graph, {budget.bytes - bytes, budget.work - work});
        auto* proof = std::get_if<analysis::PhasedValueFlowProof>(&checked);
        if (!proof) return false;
        work += proof->work;
        for (std::size_t b = 0; b < graph.phases.size(); ++b) {
          if (work == budget.work) return false;
          ++work;
          const auto& phase = graph.phases[b];
          if (phase.phase != analysis::ExceptionPhase::kCommit) continue;
          if (!phase.operation || commits[phase.operation->value]) return false;
          commits[phase.operation->value] = analysis::WorkBlockId{static_cast<std::uint32_t>(b)};
        }
        flow.emplace(std::move(proof->dominators));
      }
    }
    const auto& a = graph.ops[allocation.value]; const auto& s = graph.ops[store.value];
    if (!commits.empty()) return commits[allocation.value] &&
        flow->Dominates({commits[allocation.value]->value}, {s.block.value});
    return a.block == s.block ? a.order < s.order :
        flow->Dominates({a.block.value}, {s.block.value});
  }
};
// One deferred entry: allocated storage and a live first store that is either
// in the allocation's block after it, with no access of the cell in between,
// or the cell's only store, in another block. Local reads separately need a
// dominating store (CellInitializations); the owner tree places the store.
bool DeferredStore(const analysis::ValueProgram& graph, const CellPlan& plan, CellId cell,
    analysis::OpId store, std::uint64_t& work, DeferredActivation& activation) {
  work += std::bit_width(plan.accesses.size()) + 1;
  if (cell.value >= plan.cells.size() || store.value >= graph.ops.size() ||
      store.value >= plan.operations.size() || plan.operations[store.value] != cell) return false;
  const auto* local = std::get_if<AllocatedCell>(&plan.cells[cell.value].storage);
  const auto& op = graph.ops[store.value];
  if (!local || local->allocation.value >= graph.ops.size() || op.life != analysis::ValueLife::kLive ||
      !std::holds_alternative<analysis::ValueLexicalStore>(op.operation)) return false;
  const auto& entry = plan.cells[cell.value];
  const auto first = plan.accesses.begin() + entry.first, last = first + entry.count;
  const auto found = std::lower_bound(first, last, store,
      [](const CellAccess& access, analysis::OpId id) { return access.operation < id; });
  if (found == last || found->operation != store || found->kind != CellAccessKind::kStore) return false;
  if (graph.ops[local->allocation.value].block != op.block) {
    // The only store of this cell: count stores among its sorted accesses.
    work += entry.count;
    if (!activation.Before(graph, local->allocation, store, work)) return false;
    if (std::count_if(first, last, [](const CellAccess& access) { return access.kind == CellAccessKind::kStore; }) == 1)
      return true;
    // Several stores: the declaring store dominates every other access of the
    // cell, so it is the first execution of each path (`let t = null; …
    // t = later;`). Each check is one dominance query.
    for (auto it = first; it != last; ++it) {
      if (it->operation == store) continue;
      if (!activation.Before(graph, store, it->operation, work)) return false;
    }
    return true;
  }
  // Same block: accesses are sorted by operation and one block owns a
  // contiguous range, so the access just before this store must precede the
  // allocation, which itself precedes the store.
  return local->allocation < store && (found == first || std::prev(found)->operation < local->allocation);
}
}  // namespace
std::optional<std::uint64_t> MarkDeferredCells(const analysis::ValueProgram& graph,
    CellEvaluation& cells, std::span<const DeferredCellInitialization> deferred, analysis::SemanticBudget budget,
    std::optional<std::size_t>* rejected) {
  if (rejected) rejected->reset();
  std::uint64_t work = cells.cells.cells.size() + 1;
  if (cells.cells.revision != graph.revision || cells.cells.cfg_revision != graph.cfg_revision ||
      cells.cells.cells.size() > budget.bytes / sizeof(std::optional<analysis::OpId>)) return {};
  cells.deferred.assign(cells.cells.cells.size(), std::nullopt);
  if (cells.deferred.capacity() > budget.bytes / sizeof(std::optional<analysis::OpId>)) return {};
  DeferredActivation activation{{budget.bytes - cells.deferred.capacity() * sizeof(std::optional<analysis::OpId>), budget.work}, {}};
  for (const auto& entry : deferred) {
    if (rejected) *rejected = static_cast<std::size_t>(&entry - deferred.data());
    if (work > budget.work || entry.cell.value >= cells.deferred.size()) return {};
    ++work;
    // Several creations may defer the same cell; check each cell once.
    auto& slot = cells.deferred[entry.cell.value];
    if (slot) { if (*slot != entry.store) return {}; continue; }
    if (!DeferredStore(graph, cells.cells, entry.cell, entry.store, work, activation)) return {};
    slot = entry.store;
  }
  if (rejected) rejected->reset();
  cells.allocated_bytes += cells.deferred.capacity() * sizeof(std::optional<analysis::OpId>);
  if (work > budget.work) return {};
  return work;
}
std::optional<std::uint64_t> VerifyDeferredCells(const analysis::ValueProgram& graph,
    const CellEvaluation& cells, analysis::SemanticBudget budget) {
  std::uint64_t work = cells.deferred.size() + 1;
  if (cells.deferred.empty()) return work;
  if (cells.deferred.size() != cells.cells.cells.size() || cells.cells.revision != graph.revision ||
      cells.cells.cfg_revision != graph.cfg_revision) return {};
  DeferredActivation activation{budget, {}};
  for (std::size_t i = 0; i < cells.deferred.size(); ++i) {
    if (work > budget.work) return {};
    if (cells.deferred[i] && !DeferredStore(graph, cells.cells, CellId{static_cast<std::uint32_t>(i)},
        *cells.deferred[i], work, activation)) return {};
  }
  if (work > budget.work) return {};
  return work;
}
bool IsInitializedCellGuard(const analysis::ValueProgram& graph, const CellEvaluation& cells, analysis::OpId id) {
  return InitializedCellGuardInput(graph,cells,id).has_value();
}
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
    CellFlow flow, EvaluationInputSite* failure) {
  try {
    Quota quota(budget);
    if (!plan.environments) throw Error::kEnvironment;
    const auto& environments = *plan.environments;
    // VerifyCellInitializations checks CellPlan and its supplied environment
    // transitions independently. Rebuilding here would hide stale plan facts.
    const auto verify_initialization = flow == CellFlow::kPhased
        ? VerifyPhasedCellInitializations : VerifyCellInitializations;
    quota.Step(Take(verify_initialization(graph, environments, plan.cells,
        plan.initialization.closure_writes, plan.initialization, quota.Remaining()), Error::kInitialization));
    Identities(graph, plan.cells, plan.incoming, quota, failure);
    const auto identities=Take(analysis::AnalyzeValueIdentities(graph,quota.Remaining()),Error::kInternalIdentity);
    quota.Admit(identities.allocated_bytes);quota.Step(identities.work);
    if(plan.identities.canonical!=identities.canonical)throw Error::kInternalIdentity;
    return quota.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<CellEvaluation, CellEvaluationError> BuildEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    analysis::ClosureWrites writes, CellFlow flow, std::span<const CellDemand> demands,
    std::span<const IncomingCell> entry_demands, EvaluationInputSite* failure) {
  try {
    Quota quota(budget); quota.Admit(sizeof(CellEvaluation));
    const auto build_environments = flow == CellFlow::kPhased
        ? analysis::BuildPhasedValueEnvironments : analysis::BuildValueEnvironments;
    auto environments = Take(build_environments(graph, quota.Remaining()), Error::kEnvironment);
    quota.Admit(environments.allocated_bytes); quota.Step(environments.work);
    auto cells = Take(flow == CellFlow::kPhased
        ? BuildDemandedPhasedCellPlan(graph, environments, demands, quota.Remaining())
        : BuildDemandedCellPlan(graph, environments, demands, quota.Remaining(), entry_demands), Error::kCells);
    quota.Admit(cells.allocated_bytes); quota.Step(cells.work);
    const auto build_initialization = flow == CellFlow::kPhased
        ? BuildPhasedCellInitializations : BuildCellInitializations;
    auto initialization = Take(build_initialization(graph, environments, cells,
        writes, quota.Remaining()), Error::kInitialization);
    quota.Admit(initialization.allocated_bytes); quota.Step(initialization.work);
    Identities(graph, cells, incoming, quota, failure);
    auto identities=Take(analysis::AnalyzeValueIdentities(graph,quota.Remaining()),Error::kInternalIdentity);
    quota.Admit(identities.allocated_bytes);quota.Step(identities.work);
    TreeCheckBudget ownership(quota.Remaining());
    const auto topology_bytes = environments.allocated_bytes;
    auto topology = std::allocate_shared<analysis::ValueEnvironments>(
        RecoveryAllocator<analysis::ValueEnvironments>{ownership}, std::move(environments));
    quota.Admit(ownership.Bytes());
    topology->allocated_bytes += ownership.Bytes();
    const auto retained = sizeof(CellEvaluation) + cells.allocated_bytes + initialization.allocated_bytes +
        topology_bytes + ownership.Bytes()+identities.allocated_bytes;
    return CellEvaluation{std::move(cells), std::move(initialization), incoming, retained, quota.Work(), std::move(topology),std::move(identities)};
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<CellEvaluation, CellEvaluationError> BuildCellEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    analysis::ClosureWrites writes, std::span<const CellDemand> demands,
    std::span<const IncomingCell> entry_demands, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return BuildEvaluation(graph, budget, incoming, writes, CellFlow::kNormal, demands, entry_demands, failure);
}
std::variant<std::uint64_t, CellEvaluationError> VerifyCellEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return VerifyEvaluation(graph, plan, budget, CellFlow::kNormal, failure);
}
std::variant<CellEvaluation, CellEvaluationError> BuildPhasedCellEvaluation(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, IncomingCells incoming,
    analysis::ClosureWrites writes, std::span<const CellDemand> demands, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return BuildEvaluation(graph, budget, incoming, writes, CellFlow::kPhased, demands, {}, failure);
}
std::variant<std::uint64_t, CellEvaluationError> VerifyPhasedCellEvaluation(
    const analysis::ValueProgram& graph, const CellEvaluation& plan, analysis::SemanticBudget budget, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return VerifyEvaluation(graph, plan, budget, CellFlow::kPhased, failure);
}
}  // namespace abcd::source
