#include "cell_initialization.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/analysis/dominators.h"

namespace abcd::source {
namespace {
using Error = CellInitializationError;
enum class InitializationMode { kNormal, kPhased };
void Require(bool condition, Error error) { if (!condition) throw error; }
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : initial_(budget), left_(budget) {}
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t n, std::size_t width) { Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width; }
  template <class T> void Reserve(std::vector<T>& items, std::size_t n) {
    Admit(n, sizeof(T)); items.reserve(n); Admit(items.capacity() - n, sizeof(T));
  }
  template <class T> void Size(std::vector<T>& items, std::size_t n) { Reserve(items, n); Step(n); items.resize(n); }
  analysis::SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  analysis::SemanticBudget initial_, left_;
};
bool Language(const analysis::ValueProgram& graph, const analysis::ValueOperand& operand) {
  if (const auto* value = std::get_if<analysis::ValueId>(&operand)) return graph.values[value->value].domain.IsLanguage();
  if (const auto* special = std::get_if<analysis::SpecialValue>(&operand)) {
    switch (*special) {
      case analysis::SpecialValue::kUndefined: case analysis::SpecialValue::kNull:
      case analysis::SpecialValue::kTrue: case analysis::SpecialValue::kFalse:
      case analysis::SpecialValue::kNaN: case analysis::SpecialValue::kInfinity: return true;
    }
    return false;
  }
  return std::holds_alternative<analysis::IntegerLiteral>(operand) ||
      std::holds_alternative<analysis::Float64Bits>(operand);
}
struct InitializationFlow {
  analysis::DominatorTree dominance;
  std::vector<std::optional<analysis::WorkBlockId>> commits;
};
InitializationFlow Prepare(const analysis::ValueProgram& graph,
    const analysis::ValueEnvironments& environments, const CellPlan& cells,
    analysis::ClosureWrites writes, Quota& quota, InitializationMode mode) {
  const auto checked = mode == InitializationMode::kPhased
      ? VerifyPhasedCellPlan(graph, environments, cells, quota.Remaining())
      : VerifyCellPlan(graph, environments, cells, quota.Remaining());
  if (const auto* error = std::get_if<CellError>(&checked)) {
    if (*error == CellError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == CellError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kCells;
  }
  quota.Step(std::get<std::uint64_t>(checked));
  Require(writes == analysis::ClosureWrites::kUnknown || writes == analysis::ClosureWrites::kLanguage, Error::kClosure);
  for (const auto& op : graph.ops) {
    quota.Step(); if (op.life != analysis::ValueLife::kLive) continue;
    // Pinned RuntimeCreateObjectHavingMethod may clone methods capturing the
    // current environment. An unresolved buffer is not a pure-data literal.
    const auto* object = std::get_if<analysis::ValueObjectAllocation>(&op.operation);
    if (std::holds_alternative<analysis::ValueFunctionCreation>(op.operation) ||
        (object && object->literal))
      Require(writes == analysis::ClosureWrites::kLanguage, Error::kClosure);
    if (std::holds_alternative<analysis::ValueLexicalStore>(op.operation))
      Require(Language(graph, graph.uses[op.first_use.value].operand), Error::kDomain);
    if (std::holds_alternative<analysis::ValueLexicalLoad>(op.operation))
      Require(graph.values[op.result->value].domain.IsLanguage(), Error::kDomain);
  }
  InitializationFlow result;
  if (mode == InitializationMode::kPhased) quota.Size(result.commits, graph.ops.size());
  analysis::NormalFlow flow; quota.Size(flow.blocks, graph.blocks.size());
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Step(); const auto& block = graph.blocks[b]; auto& target = flow.blocks[b];
    if (mode == InitializationMode::kPhased) {
      const auto& phase = graph.phases[b];
      if (phase.phase == analysis::ExceptionPhase::kCommit) {
        Require(phase.operation && !result.commits[phase.operation->value], Error::kCells);
        result.commits[phase.operation->value] = analysis::WorkBlockId{static_cast<std::uint32_t>(b)};
      }
    }
    target.successor_count = block.edge_count;
    for (std::uint8_t e = 0; e < block.edge_count; ++e) {
      quota.Step(); const auto& edge = graph.edges[block.first_edge.value + e];
      // Dominance consumes connectivity, not edge spelling. Preserve every
      // exception target; only the temporary topology adapter relabels edges.
      const auto kind = mode == InitializationMode::kPhased && edge.kind == analysis::FlowEdgeKind::kException
          ? analysis::FlowEdgeKind::kTaken : edge.kind;
      target.successors[e] = {{edge.target.value}, kind};
    }
  }
  const auto budget = quota.Remaining();
  auto built = analysis::BuildNormalDominators(flow, {}, {budget.bytes, budget.work});
  if (const auto* error = std::get_if<analysis::DominatorError>(&built)) {
    if (*error == analysis::DominatorError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::DominatorError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kCells;
  }
  result.dominance = std::get<analysis::DominatorTree>(std::move(built));
  quota.Admit(result.dominance.work.allocated_bytes, 1); quota.Step(result.dominance.work.steps);
  return result;
}
bool Before(const analysis::ValueProgram& graph, const InitializationFlow& flow,
    analysis::OpId a, analysis::OpId b) {
  const auto& first = graph.ops[a.value]; const auto& second = graph.ops[b.value];
  if (!flow.commits.empty()) {
    const auto commit = flow.commits[a.value];
    return commit && flow.dominance.Dominates({commit->value}, {second.block.value});
  }
  return first.block == second.block ? first.order < second.order :
      flow.dominance.Dominates({first.block.value}, {second.block.value});
}
struct Undo { CellId cell; std::optional<analysis::OpId> previous; };
struct Frame { std::uint32_t end; std::size_t undo; };
std::variant<std::uint64_t, CellInitializationError> VerifyInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, const CellInitializations& plan,
    analysis::SemanticBudget budget, InitializationMode mode) {
  try {
    Quota quota(budget);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.closure_writes == writes && plan.reads.size() == graph.ops.size(), Error::kWitness);
    const auto dom = Prepare(graph, environments, cells, writes, quota, mode);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step(); const auto& op = graph.ops[i];
      if (op.life != analysis::ValueLife::kLive || !std::holds_alternative<analysis::ValueLexicalLoad>(op.operation)) {
        Require(std::holds_alternative<NotCellRead>(plan.reads[i]), Error::kWitness); continue;
      }
      const auto cell = *cells.operations[i]; const auto& storage = cells.cells[cell.value].storage;
      if (std::holds_alternative<IncomingCell>(storage)) {
        Require(std::holds_alternative<IncomingInitialization>(plan.reads[i]), Error::kWitness); continue;
      }
      const auto* witness = std::get_if<StoredInitialization>(&plan.reads[i]);
      Require(witness && witness->store.value < graph.ops.size() && cells.operations[witness->store.value] == cell &&
          graph.ops[witness->store.value].life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueLexicalStore>(graph.ops[witness->store.value].operation), Error::kWitness);
      const auto allocation = std::get<AllocatedCell>(storage).allocation;
      Require(Before(graph, dom, allocation, witness->store) &&
          Before(graph, dom, witness->store, {static_cast<std::uint32_t>(i)}), Error::kUninitialized);
    }
    return quota.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<CellInitializations, CellInitializationError> BuildInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, analysis::SemanticBudget budget,
    InitializationMode mode) {
  try {
    Quota quota(budget); const auto dom = Prepare(graph, environments, cells, writes, quota, mode);
    quota.Admit(1, sizeof(CellInitializations));
    CellInitializations plan{graph.revision, graph.cfg_revision, writes}; quota.Size(plan.reads, graph.ops.size());
    std::vector<std::optional<analysis::OpId>> state; quota.Size(state, cells.cells.size());
    std::vector<std::optional<CellId>> heads, next;
    quota.Size(heads, graph.ops.size()); quota.Size(next, cells.cells.size());
    for (std::size_t c = 0; c < cells.cells.size(); ++c) {
      quota.Step(); if (const auto* allocation = std::get_if<AllocatedCell>(&cells.cells[c].storage)) {
        next[c] = heads[allocation->allocation.value]; heads[allocation->allocation.value] = CellId{static_cast<std::uint32_t>(c)};
      }
    }
    std::vector<analysis::WorkBlockId> order; quota.Size(order, graph.blocks.size());
    std::size_t reachable = 0;
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      quota.Step(); const auto begin = dom.dominance.nodes[b].begin;
      // Fresh phased flow admits only explicit throw's provenance-only dead
      // Commit. It neither executes stores nor introduces a source binding.
      if (begin == 0 && mode == InitializationMode::kPhased) continue;
      Require(begin != 0 && begin <= order.size(), Error::kCells);
      order[begin - 1] = {static_cast<std::uint32_t>(b)}; ++reachable;
    }
    order.resize(reachable);
    Require(cells.cells.size() <= std::numeric_limits<std::size_t>::max() - graph.ops.size(), Error::kResourceLimit);
    std::vector<Undo> undo; quota.Reserve(undo, graph.ops.size() + cells.cells.size());
    std::vector<Frame> stack; quota.Reserve(stack, graph.blocks.size());
    const auto set = [&](CellId cell, std::optional<analysis::OpId> value) {
      quota.Step(); Require(undo.size() < undo.capacity(), Error::kResourceLimit);
      undo.push_back({cell, state[cell.value]}); state[cell.value] = value;
    };
    const auto apply = [&](analysis::OpId id) {
      quota.Step();
      for (auto cell = heads[id.value]; cell; cell = next[cell->value]) set(*cell, {});
      if (!cells.operations[id.value]) return;
      const auto cell = *cells.operations[id.value]; const auto& op = graph.ops[id.value];
      if (std::holds_alternative<analysis::ValueLexicalStore>(op.operation)) set(cell, id);
      else if (std::holds_alternative<IncomingCell>(cells.cells[cell.value].storage)) plan.reads[id.value] = IncomingInitialization{};
      else { Require(state[cell.value].has_value(), Error::kUninitialized); plan.reads[id.value] = StoredInitialization{*state[cell.value]}; }
    };
    for (const auto block : order) {
      quota.Step(); const auto interval = dom.dominance.nodes[block.value];
      while (!stack.empty() && stack.back().end <= interval.begin) {
        quota.Step(); const auto marker = stack.back().undo; stack.pop_back();
        while (undo.size() > marker) { quota.Step(); const auto old = undo.back(); undo.pop_back(); state[old.cell.value] = old.previous; }
      }
      stack.push_back({interval.end, undo.size()});
      if (mode == InitializationMode::kPhased) {
        const auto& phase = graph.phases[block.value];
        if (!phase.operation) continue;
        const auto& operation = graph.ops[phase.operation->value].operation;
        if ((phase.phase == analysis::ExceptionPhase::kBefore &&
                std::holds_alternative<analysis::ValueLexicalLoad>(operation)) ||
            (phase.phase == analysis::ExceptionPhase::kCommit &&
                (std::holds_alternative<analysis::ValueEnvironmentPush>(operation) ||
                 std::holds_alternative<analysis::ValueLexicalStore>(operation))))
          apply(*phase.operation);
        continue;
      }
      const auto& range = graph.blocks[block.value];
      for (std::uint32_t j = 0; j < range.op_count; ++j) {
        apply({range.first_op.value + j});
      }
    }
    const auto checked = VerifyInitializations(graph, environments, cells, writes, plan, quota.Remaining(), mode);
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = sizeof(plan) + plan.reads.capacity() * sizeof(CellReadInitialization);
    plan.work = quota.Work(); return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<CellInitializations, CellInitializationError> BuildCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, analysis::SemanticBudget budget) {
  return BuildInitializations(graph, environments, cells, writes, budget, InitializationMode::kNormal);
}
std::variant<std::uint64_t, CellInitializationError> VerifyCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, const CellInitializations& plan,
    analysis::SemanticBudget budget) {
  return VerifyInitializations(graph, environments, cells, writes, plan, budget, InitializationMode::kNormal);
}
std::variant<CellInitializations, CellInitializationError> BuildPhasedCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, analysis::SemanticBudget budget) {
  return BuildInitializations(graph, environments, cells, writes, budget, InitializationMode::kPhased);
}
std::variant<std::uint64_t, CellInitializationError> VerifyPhasedCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, const CellInitializations& plan,
    analysis::SemanticBudget budget) {
  return VerifyInitializations(graph, environments, cells, writes, plan, budget, InitializationMode::kPhased);
}
}  // namespace abcd::source
