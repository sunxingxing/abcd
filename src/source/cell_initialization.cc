#include "cell_initialization.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/analysis/dominators.h"

namespace abcd::source {
namespace {
using Error = CellInitializationError;
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
analysis::DominatorTree Prepare(const analysis::ValueProgram& graph,
    const analysis::ValueEnvironments& environments, const CellPlan& cells,
    analysis::ClosureWrites writes, Quota& quota) {
  const auto checked = VerifyCellPlan(graph, environments, cells, quota.Remaining());
  if (const auto* error = std::get_if<CellError>(&checked)) {
    if (*error == CellError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == CellError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kCells;
  }
  quota.Step(std::get<std::uint64_t>(checked));
  Require(writes == analysis::ClosureWrites::kUnknown || writes == analysis::ClosureWrites::kLanguage, Error::kClosure);
  for (const auto& op : graph.ops) {
    quota.Step(); if (op.life != analysis::ValueLife::kLive) continue;
    if (std::holds_alternative<analysis::ValueFunctionCreation>(op.operation))
      Require(writes == analysis::ClosureWrites::kLanguage, Error::kClosure);
    if (std::holds_alternative<analysis::ValueLexicalStore>(op.operation))
      Require(Language(graph, graph.uses[op.first_use.value].operand), Error::kDomain);
    if (std::holds_alternative<analysis::ValueLexicalLoad>(op.operation))
      Require(graph.values[op.result->value].domain.IsLanguage(), Error::kDomain);
  }
  analysis::NormalFlow flow; quota.Size(flow.blocks, graph.blocks.size());
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Step(); const auto& block = graph.blocks[b]; auto& target = flow.blocks[b];
    target.successor_count = block.edge_count;
    for (std::uint8_t e = 0; e < block.edge_count; ++e) {
      quota.Step(); const auto& edge = graph.edges[block.first_edge.value + e];
      target.successors[e] = {{edge.target.value}, edge.kind};
    }
  }
  const auto budget = quota.Remaining();
  auto built = analysis::BuildNormalDominators(flow, {}, {budget.bytes, budget.work});
  if (const auto* error = std::get_if<analysis::DominatorError>(&built)) {
    if (*error == analysis::DominatorError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::DominatorError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kCells;
  }
  auto result = std::get<analysis::DominatorTree>(std::move(built));
  quota.Admit(result.work.allocated_bytes, 1); quota.Step(result.work.steps);
  return result;
}
bool Before(const analysis::ValueProgram& graph, const analysis::DominatorTree& dom,
    analysis::OpId a, analysis::OpId b) {
  const auto& first = graph.ops[a.value]; const auto& second = graph.ops[b.value];
  return first.block == second.block ? first.order < second.order :
      dom.Dominates({first.block.value}, {second.block.value});
}
struct Undo { CellId cell; std::optional<analysis::OpId> previous; };
struct Frame { std::uint32_t end; std::size_t undo; };
}  // namespace

std::variant<std::uint64_t, CellInitializationError> VerifyCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, const CellInitializations& plan,
    analysis::SemanticBudget budget) {
  try {
    Quota quota(budget);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(plan.closure_writes == writes && plan.reads.size() == graph.ops.size(), Error::kWitness);
    const auto dom = Prepare(graph, environments, cells, writes, quota);
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

std::variant<CellInitializations, CellInitializationError> BuildCellInitializations(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, analysis::ClosureWrites writes, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); const auto dom = Prepare(graph, environments, cells, writes, quota);
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
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      quota.Step(); const auto begin = dom.nodes[b].begin;
      Require(begin != 0 && begin <= order.size(), Error::kCells); order[begin - 1] = {static_cast<std::uint32_t>(b)};
    }
    Require(cells.cells.size() <= std::numeric_limits<std::size_t>::max() - graph.ops.size(), Error::kResourceLimit);
    std::vector<Undo> undo; quota.Reserve(undo, graph.ops.size() + cells.cells.size());
    std::vector<Frame> stack; quota.Reserve(stack, graph.blocks.size());
    const auto set = [&](CellId cell, std::optional<analysis::OpId> value) {
      quota.Step(); Require(undo.size() < undo.capacity(), Error::kResourceLimit);
      undo.push_back({cell, state[cell.value]}); state[cell.value] = value;
    };
    for (const auto block : order) {
      quota.Step(); const auto interval = dom.nodes[block.value];
      while (!stack.empty() && stack.back().end <= interval.begin) {
        quota.Step(); const auto marker = stack.back().undo; stack.pop_back();
        while (undo.size() > marker) { quota.Step(); const auto old = undo.back(); undo.pop_back(); state[old.cell.value] = old.previous; }
      }
      stack.push_back({interval.end, undo.size()});
      const auto& range = graph.blocks[block.value];
      for (std::uint32_t j = 0; j < range.op_count; ++j) {
        quota.Step(); const analysis::OpId id{range.first_op.value + j};
        for (auto cell = heads[id.value]; cell; cell = next[cell->value]) set(*cell, {});
        if (!cells.operations[id.value]) continue;
        const auto cell = *cells.operations[id.value]; const auto& op = graph.ops[id.value];
        if (std::holds_alternative<analysis::ValueLexicalStore>(op.operation)) set(cell, id);
        else if (std::holds_alternative<IncomingCell>(cells.cells[cell.value].storage)) plan.reads[id.value] = IncomingInitialization{};
        else { Require(state[cell.value].has_value(), Error::kUninitialized); plan.reads[id.value] = StoredInitialization{*state[cell.value]}; }
      }
    }
    const auto checked = VerifyCellInitializations(graph, environments, cells, writes, plan, quota.Remaining());
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = sizeof(plan) + plan.reads.capacity() * sizeof(CellReadInitialization);
    plan.work = quota.Work(); return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
