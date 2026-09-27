#include "cell_plan.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>

namespace abcd::source {
namespace {
using Error = CellError;
enum class CellFlow { kNormal, kPhased };
void Require(bool condition, Error error) { if (!condition) throw error; }
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : initial_(budget), left_(budget) {}
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t n, std::size_t width) { Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width; }
  template <class T> void Size(std::vector<T>& items, std::size_t n) {
    Admit(n, sizeof(T)); Step(n); items.resize(n); Admit(items.capacity() - n, sizeof(T));
  }
  analysis::SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  analysis::SemanticBudget initial_, left_;
};
auto Key(const CellStorage& storage) {
  // The variant discriminator keeps local allocation sites disjoint from
  // numerically equal incoming depths. Slot spelling is never consulted.
  if (const auto* local = std::get_if<AllocatedCell>(&storage))
    return std::tuple(storage.index(), local->allocation.value, local->slot.value);
  const auto& incoming = std::get<IncomingCell>(storage);
  return std::tuple(storage.index(), incoming.depth.value, incoming.slot.value);
}
void Environment(const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    Quota& quota, CellFlow flow) {
  const auto checked = flow == CellFlow::kPhased
      ? analysis::VerifyPhasedValueEnvironments(graph, environments, quota.Remaining())
      : analysis::VerifyValueEnvironments(graph, environments, quota.Remaining());
  if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&checked)) {
    if (*error == analysis::ValueEnvironmentError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::ValueEnvironmentError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kEnvironment;
  }
  quota.Step(std::get<std::uint64_t>(checked));
}
std::vector<analysis::OpId> Allocations(const analysis::ValueEnvironments& environments, Quota& quota) {
  std::vector<analysis::OpId> result;
  quota.Size(result, environments.flow.environments.Size());
  for (const auto& item : environments.flow.allocations) {
    quota.Step(); result[item.environment.value] = {item.instruction};
  }
  return result;
}
CellStorage Storage(const analysis::LexicalBinding& binding, std::span<const analysis::OpId> allocations) {
  if (const auto* local = std::get_if<analysis::EnvironmentId>(&binding.environment)) {
    Require(local->value < allocations.size(), Error::kStorage);
    return AllocatedCell{allocations[local->value], binding.slot};
  }
  return IncomingCell{std::get<analysis::CapturedDepth>(binding.environment), binding.slot};
}
CellAccessKind Kind(const analysis::ValueOp& op) {
  Require(op.life == analysis::ValueLife::kLive, Error::kAccess);
  if (std::holds_alternative<analysis::ValueLexicalLoad>(op.operation)) {
    Require(op.result.has_value() && op.use_count == 0, Error::kAccess);
    return CellAccessKind::kLoad;
  }
  Require(std::holds_alternative<analysis::ValueLexicalStore>(op.operation) &&
      !op.result && op.use_count == 1, Error::kAccess);
  return CellAccessKind::kStore;
}
struct Candidate { CellStorage storage; CellAccess access; };
}  // namespace

std::optional<CellId> FindCell(const CellPlan& plan, const CellStorage& storage) {
  const auto found = std::lower_bound(plan.cells.begin(), plan.cells.end(), storage,
      [](const Cell& cell, const CellStorage& key) { return Key(cell.storage) < Key(key); });
  if (found == plan.cells.end() || Key(found->storage) != Key(storage)) return {};
  return CellId{static_cast<std::uint32_t>(found - plan.cells.begin())};
}

namespace {
std::variant<std::uint64_t, CellError> VerifyPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& plan, analysis::SemanticBudget budget, CellFlow flow) {
  try {
    Quota quota(budget);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Environment(graph, environments, quota, flow);
    Require(plan.operations.size() == graph.ops.size() && plan.accesses.size() == environments.flow.bindings.size(), Error::kCoverage);
    auto allocations = Allocations(environments, quota);
    Require(plan.allocations.size() == allocations.size(), Error::kStorage);
    for (std::size_t i = 0; i < allocations.size(); ++i) {
      quota.Step(); Require(plan.allocations[i] == allocations[i], Error::kStorage);
    }
    std::vector<std::uint8_t> seen;
    quota.Size(seen, graph.ops.size());
    std::size_t next = 0;
    for (std::size_t c = 0; c < plan.cells.size(); ++c) {
      quota.Step(); const auto& cell = plan.cells[c];
      Require(c <= std::numeric_limits<std::uint32_t>::max() && cell.first == next && cell.count != 0 &&
          cell.count <= plan.accesses.size() - next, Error::kCoverage);
      Require(c == 0 || Key(plan.cells[c - 1].storage) < Key(cell.storage), Error::kStorage);
      std::optional<analysis::OpId> previous;
      for (std::uint32_t i = 0; i < cell.count; ++i) {
        quota.Step(); const auto& access = plan.accesses[next++];
        Require(access.operation.value < graph.ops.size() && !seen[access.operation.value] &&
            (!previous || *previous < access.operation), Error::kAccess);
        const auto& op = graph.ops[access.operation.value];
        Require(access.kind == Kind(op) && plan.operations[access.operation.value] == CellId{static_cast<std::uint32_t>(c)}, Error::kAccess);
        if (access.kind == CellAccessKind::kStore) {
          const auto& use = graph.uses[op.first_use.value];
          Require(use.role == analysis::OperandRole::kStoredValue && use.purpose == analysis::ReadPurpose::kOperand, Error::kAccess);
        }
        seen[access.operation.value] = 1; previous = access.operation;
      }
    }
    Require(next == plan.accesses.size(), Error::kCoverage);
    for (const auto& binding : environments.flow.bindings) {
      quota.Step(); const auto cell = plan.operations[binding.instruction];
      Require(cell && cell->value < plan.cells.size() && seen[binding.instruction] &&
          Key(plan.cells[cell->value].storage) == Key(Storage(binding.binding, allocations)), Error::kStorage);
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step(); Require(plan.operations[i].has_value() == (seen[i] != 0), Error::kCoverage);
    }
    return quota.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<CellPlan, CellError> BuildPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    analysis::SemanticBudget budget, CellFlow flow) {
  try {
    Quota quota(budget); Environment(graph, environments, quota, flow);
    Require(environments.flow.bindings.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    quota.Admit(1, sizeof(CellPlan));
    auto allocations = Allocations(environments, quota);
    std::vector<Candidate> candidates;
    quota.Size(candidates, environments.flow.bindings.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      quota.Step(); const auto& binding = environments.flow.bindings[i];
      candidates[i] = {Storage(binding.binding, allocations), {{binding.instruction}, Kind(graph.ops[binding.instruction])}};
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
      quota.Step(); return std::tuple(Key(a.storage), a.access.operation) < std::tuple(Key(b.storage), b.access.operation);
    });
    std::size_t count = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      quota.Step(); count += i == 0 || Key(candidates[i - 1].storage) != Key(candidates[i].storage);
    }
    CellPlan plan{graph.revision, graph.cfg_revision};
    quota.Size(plan.cells, count); quota.Size(plan.accesses, candidates.size()); quota.Size(plan.operations, graph.ops.size());
    count = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      quota.Step(); const auto& candidate = candidates[i];
      if (i == 0 || Key(candidates[i - 1].storage) != Key(candidate.storage))
        plan.cells[count++] = {candidate.storage, static_cast<std::uint32_t>(i), 0};
      ++plan.cells[count - 1].count; plan.accesses[i] = candidate.access;
      plan.operations[candidate.access.operation.value] = CellId{static_cast<std::uint32_t>(count - 1)};
    }
    plan.allocations = std::move(allocations);
    const auto checked = VerifyPlan(graph, environments, plan, quota.Remaining(), flow);
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = sizeof(plan) + plan.cells.capacity() * sizeof(Cell) +
        plan.accesses.capacity() * sizeof(CellAccess) + plan.operations.capacity() * sizeof(std::optional<CellId>) +
        plan.allocations.capacity() * sizeof(analysis::OpId);
    plan.work = quota.Work(); return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<CellPlan, CellError> BuildCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    analysis::SemanticBudget budget) {
  return BuildPlan(graph, environments, budget, CellFlow::kNormal);
}
std::variant<std::uint64_t, CellError> VerifyCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& plan, analysis::SemanticBudget budget) {
  return VerifyPlan(graph, environments, plan, budget, CellFlow::kNormal);
}
std::variant<CellPlan, CellError> BuildPhasedCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    analysis::SemanticBudget budget) {
  return BuildPlan(graph, environments, budget, CellFlow::kPhased);
}
std::variant<std::uint64_t, CellError> VerifyPhasedCellPlan(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& plan, analysis::SemanticBudget budget) {
  return VerifyPlan(graph, environments, plan, budget, CellFlow::kPhased);
}
}  // namespace abcd::source
