#include "cell_defaults.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = CellDefaultError;
struct BlockSummary {
  std::optional<OpId> last, store;
  std::size_t predecessors = 0;
  bool arm = true;
  bool visited = false;
};
class Finder {
 public:
  Finder(const ValueProgram& graph, const CellPlan& cells, const ParameterPlan& parameters,
      SemanticBudget budget) : graph_(graph), cells_(cells), parameters_(parameters), initial_(budget), left_(budget) {}
  CellDefaults Find(std::uint32_t length) {
    Require(cells_.revision == graph_.revision && cells_.cfg_revision == graph_.cfg_revision &&
        parameters_.revision == graph_.revision && parameters_.cfg_revision == graph_.cfg_revision, Error::kRevision);
    Require(length <= parameters_.formals.size(), Error::kParameters);
    Require(cells_.operations.size() == graph_.ops.size(), Error::kGraph);
    Require(graph_.ops.size() < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    CellDefaults result{graph_.revision, graph_.cfg_revision};
    Admit(1, sizeof(Finder));
    Reserve(blocks_, graph_.blocks.size()); blocks_.resize(graph_.blocks.size());
    for (const auto& edge : graph_.edges) {
      Step(); Require(edge.target.value < blocks_.size(), Error::kGraph);
      ++blocks_[edge.target.value].predecessors;
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); const auto& op = graph_.ops[i];
      Require(op.block.value < blocks_.size(), Error::kGraph);
      if (op.life != ValueLife::kLive || std::holds_alternative<ValueNop>(op.operation)) continue;
      auto& block = blocks_[op.block.value];
      const OpId id{static_cast<std::uint32_t>(i)};
      block.last = id;
      if (std::holds_alternative<ValueLexicalStore>(op.operation)) {
        if (block.store) block.arm = false;
        block.store = id;
      } else if (!std::holds_alternative<ValueJump>(op.operation) &&
                 !std::holds_alternative<ValueCopy>(op.operation) &&
                 !std::holds_alternative<ValueString>(op.operation)) block.arm = false;
    }
    Reserve(result.candidates, parameters_.formals.size() - length);
    WorkBlockId header{0};
    auto next = length;
    while (next < parameters_.formals.size() && header.value < blocks_.size() && !blocks_[header.value].visited) {
      Step(); blocks_[header.value].visited = true;
      auto candidate = Match(header);
      if (!candidate || candidate->parameter < next ||
          (result.candidates.empty() && candidate->parameter != length)) break;
      result.candidates.push_back(*candidate);
      next = candidate->parameter + 1; header = candidate->join;
    }
    result.allocated_bytes = initial_.bytes - left_.bytes;
    result.work = initial_.work - left_.work;
    return result;
  }
 private:
  void Require(bool ok, Error error) const { if (!ok) throw error; }
  void Step() { Require(left_.work != 0, Error::kWorkLimit); --left_.work; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Reserve(std::vector<T>& array, std::size_t count) {
    Admit(count, sizeof(T)); array.reserve(count); Admit(array.capacity() - count, sizeof(T));
  }
  const ValueOperand& Operand(OpId id, std::uint32_t ordinal) {
    Require(id.value < graph_.ops.size(), Error::kGraph);
    const auto& op = graph_.ops[id.value];
    Require(ordinal < op.use_count && op.first_use.value < graph_.uses.size() &&
        ordinal < graph_.uses.size() - op.first_use.value, Error::kGraph);
    return graph_.uses[op.first_use.value + ordinal].operand;
  }
  std::optional<OpId> Producer(const ValueOperand& operand) const {
    const auto* id = std::get_if<ValueId>(&operand);
    if (!id || id->value >= graph_.values.size()) return {};
    const auto* result = std::get_if<OpResult>(&graph_.values[id->value].origin);
    if (!result || result->op.value >= graph_.ops.size() || graph_.ops[result->op.value].life != ValueLife::kLive) return {};
    return result->op;
  }
  std::optional<ValueOperand> Literal(const ValueOperand& operand) {
    if (!std::holds_alternative<ValueId>(operand)) return operand;
    const auto op = Producer(operand);
    if (op && std::holds_alternative<ValueString>(graph_.ops[op->value].operation) && graph_.ops[op->value].use_count == 0) return operand;
    if (!op || !std::holds_alternative<ValueCopy>(graph_.ops[op->value].operation) || graph_.ops[op->value].use_count != 1) return {};
    const auto& input = Operand(*op, 0);
    if (std::holds_alternative<ValueId>(input)) return {};
    return input;
  }
  bool Undefined(const ValueOperand& operand) {
    const auto literal = Literal(operand);
    const auto* value = literal ? std::get_if<SpecialValue>(&*literal) : nullptr;
    return value && *value == SpecialValue::kUndefined;
  }
  std::optional<CellDefault> Match(WorkBlockId header) {
    const auto& block = graph_.blocks[header.value];
    const auto branch = blocks_[header.value].last;
    if (!branch || block.edge_count != 2 || (header.value == 0 && blocks_[0].predecessors != 0)) return {};
    const auto* condition = std::get_if<ValueBranch>(&graph_.ops[branch->value].operation);
    if (!condition || graph_.ops[branch->value].use_count != 1) return {};
    const bool positive = condition->predicate == UnaryOperator::kTrueOrNonzeroNumber;
    if (!positive && condition->predicate != UnaryOperator::kFalseOrZero) return {};
    const auto comparison = Producer(Operand(*branch, 0));
    if (!comparison) return {};
    const auto& compare = graph_.ops[comparison->value];
    const auto* binary = std::get_if<ValueBinary>(&compare.operation);
    if (!binary || compare.block != header || compare.use_count != 2 ||
        (binary->op != BinaryOperator::kStrictEqual && binary->op != BinaryOperator::kStrictNotEqual)) return {};
    const auto load = Undefined(Operand(*comparison, 0)) ? Producer(Operand(*comparison, 1)) :
        Undefined(Operand(*comparison, 1)) ? Producer(Operand(*comparison, 0)) : std::nullopt;
    if (!load || graph_.ops[load->value].block != header ||
        !std::holds_alternative<ValueLexicalLoad>(graph_.ops[load->value].operation)) return {};
    const auto cell = cells_.operations[load->value];
    if (!cell || cell->value >= cells_.cells.size()) return {};
    const auto& storage = cells_.cells[cell->value];
    if (!std::holds_alternative<AllocatedCell>(storage.storage) || storage.count < 2) return {};
    Require(storage.first < cells_.accesses.size() && storage.count <= cells_.accesses.size() - storage.first, Error::kGraph);
    const auto first = cells_.accesses[storage.first];
    if (first.kind != CellAccessKind::kStore || cells_.accesses[storage.first + 1].operation != *load) return {};
    const auto* entry = std::get_if<ValueId>(&Operand(first.operation, 0));
    if (!entry || entry->value >= graph_.values.size()) return {};
    const auto* definition = std::get_if<EntryDefinition>(&graph_.values[entry->value].origin);
    const auto* parameter = definition ? std::get_if<ParameterEntry>(definition) : nullptr;
    if (!parameter || parameter->index >= parameters_.formals.size() || parameters_.formals[parameter->index].entry != *entry) return {};
    Require(block.first_edge.value < graph_.edges.size() && block.edge_count <= graph_.edges.size() - block.first_edge.value, Error::kGraph);
    std::optional<EdgeId> missing, present;
    const bool taken = (binary->op == BinaryOperator::kStrictEqual) == positive;
    for (std::uint32_t i = 0; i < block.edge_count; ++i) {
      const EdgeId edge{block.first_edge.value + i};
      const auto kind = graph_.edges[edge.value].kind;
      if (kind != FlowEdgeKind::kTaken && kind != FlowEdgeKind::kFallthrough) return {};
      auto& target = ((kind == FlowEdgeKind::kTaken) == taken) ? missing : present;
      if (target) return {};
      target = edge;
    }
    if (!missing || !present) return {};
    const auto arm = graph_.edges[missing->value].target, join = graph_.edges[present->value].target;
    if (arm == header || join == header || arm == join || blocks_[arm.value].predecessors != 1 ||
        blocks_[join.value].predecessors != 2 || !blocks_[arm.value].arm || !blocks_[arm.value].store) return {};
    const auto& arm_block = graph_.blocks[arm.value];
    if (arm_block.edge_count != 1 || arm_block.first_edge.value >= graph_.edges.size() ||
        graph_.edges[arm_block.first_edge.value].target != join) return {};
    const auto store = *blocks_[arm.value].store;
    if (cells_.operations[store.value] != cell) return {};
    const auto literal = Literal(Operand(store, 0));
    if (!literal) return {};
    std::optional<abc::PoolIndex> text;
    if (const auto source = Producer(*literal)) {
      const auto* string = std::get_if<ValueString>(&graph_.ops[source->value].operation);
      if (!string) return {};
      text = string->string;
    }
    return CellDefault{parameter->index, *entry, *cell, first.operation, *load, *comparison,
        *branch, store, *literal, header, arm, join, *present, *missing, arm_block.first_edge, text};
  }
  const ValueProgram& graph_;
  const CellPlan& cells_;
  const ParameterPlan& parameters_;
  SemanticBudget initial_, left_;
  std::vector<BlockSummary> blocks_;
};
}  // namespace
std::variant<CellDefaults, CellDefaultError> FindCellDefaults(const analysis::ValueProgram& graph,
    const CellPlan& cells, const ParameterPlan& parameters, std::uint32_t length, analysis::SemanticBudget budget) {
  try { return Finder(graph, cells, parameters, budget).Find(length);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
