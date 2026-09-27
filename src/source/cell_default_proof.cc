#include "cell_default_proof.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = CellDefaultProofError;
void Require(bool ok, Error error) { if (!ok) throw error; }
enum class PrefixKind : std::uint8_t { kHeader, kArm };
struct PrefixBlock { std::uint32_t parameter; PrefixKind kind; };
bool Equal(const ValueOperand& a, const ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_enum_v<T>) return value == std::get<T>(b);
    else return value.value == std::get<T>(b).value;
  }, a);
}
class Checker {
 public:
  Checker(const ValueProgram& graph, const CellPlan& cells, const ParameterPlan& parameters,
      const CellDefaults& candidates, SemanticBudget budget)
      : graph_(graph), cells_(cells), parameters_(parameters), candidates_(candidates), initial_(budget), left_(budget) {}
  std::vector<DefaultOpPlacement> Check(std::uint32_t length) {
    Require(candidates_.revision == graph_.revision && candidates_.cfg_revision == graph_.cfg_revision &&
        parameters_.revision == graph_.revision && parameters_.cfg_revision == graph_.cfg_revision, Error::kRevision);
    Require(!candidates_.candidates.empty() && length < parameters_.formals.size() &&
        candidates_.candidates.front().parameter == length, Error::kParameter);
    Require(graph_.ops.size() < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Size(prefix_, graph_.blocks.size()); Size(predecessors_, graph_.blocks.size());
    Size(moved_, graph_.ops.size()); Size(kept_, graph_.ops.size());
    Size(restricted_, graph_.values.size()); Size(candidate_cells_, cells_.cells.size());
    for (const auto& edge : graph_.edges) {
      Step(); Require(edge.target.value < predecessors_.size(), Error::kControl); ++predecessors_[edge.target.value];
    }
    for (std::size_t i = 0; i < candidates_.candidates.size(); ++i) { Step(); Candidate(i); }
    Require(predecessors_[0] == 0, Error::kControl);
    Require(!prefix_[candidates_.candidates.back().join.value], Error::kControl);
    std::vector<DefaultOpPlacement> operations;
    Reserve(operations, graph_.ops.size());
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); const auto& op = graph_.ops[i];
      Require(op.block.value < prefix_.size(), Error::kControl);
      if (op.life != ValueLife::kLive) continue;
      const OpId id{static_cast<std::uint32_t>(i)};
      const auto prefix = prefix_[op.block.value];
      if (moved_[i]) {
        Require(prefix.has_value(), Error::kControl);
        operations.push_back({id, *moved_[i]}); continue;
      }
      if (!prefix) continue;
      if (std::holds_alternative<ValueNop>(op.operation)) { Pure(op); continue; }
      if (prefix->kind == PrefixKind::kArm && std::holds_alternative<ValueJump>(op.operation)) {
        Pure(op); operations.push_back({id, prefix->parameter}); continue;
      }
      Require(prefix->kind == PrefixKind::kHeader, Error::kEffect);
      if (std::holds_alternative<ValueEnvironmentPush>(op.operation)) continue;
      if (std::holds_alternative<ValueLexicalStore>(op.operation)) {
        HeaderStore(id);
        continue;
      }
      // Includes calls, closures, unrelated loads, pop/current-environment,
      // module/global reads and arbitrary computations: none may be hidden by
      // moving a native default ahead of this prefix.
      throw Error::kEffect;
    }
    for (const auto& value : graph_.values) {
      Step();
      if (value.life != ValueLife::kLive) continue;
      if (const auto* phi = std::get_if<PhiDefinition>(&value.origin))
        Require(phi->block.value < prefix_.size() && !prefix_[phi->block.value], Error::kControl);
    }
    for (const auto& use : graph_.uses) {
      Step(); if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (!value) continue;
      Require(value->value < restricted_.size(), Error::kShape);
      if (const auto owner = restricted_[value->value]) {
        const auto* actual = std::get_if<OpId>(&use.owner);
        Require(actual && *actual == *owner && use.purpose == ReadPurpose::kOperand, Error::kEscape);
      }
    }
    return operations;
  }
  SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t n, std::size_t width) { Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width; }
  template<class T> void Reserve(std::vector<T>& values, std::size_t n) {
    Admit(n, sizeof(T)); values.reserve(n); Admit(values.capacity() - n, sizeof(T));
  }
  template<class T> void Size(std::vector<T>& values, std::size_t n) { Reserve(values, n); Step(n); values.resize(n); }
  const ValueOp& Op(OpId id) const {
    Require(id.value < graph_.ops.size() && graph_.ops[id.value].life == ValueLife::kLive, Error::kShape);
    return graph_.ops[id.value];
  }
  const ValueOperand& Operand(OpId id, std::uint32_t ordinal, OperandRole role) const {
    const auto& op = Op(id);
    Require(ordinal < op.use_count && op.first_use.value < graph_.uses.size() &&
        ordinal < graph_.uses.size() - op.first_use.value, Error::kShape);
    const auto& use = graph_.uses[op.first_use.value + ordinal];
    Require(use.life == ValueLife::kLive && use.owner == std::variant<OpId, ValueId>{id} &&
        use.role == role && use.purpose == ReadPurpose::kOperand, Error::kShape);
    return use.operand;
  }
  void Pure(const ValueOp& op) const {
    Require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
        op.exceptional == ExceptionalCompletion::kImpossible, Error::kEffect);
  }
  void Restrict(ValueId value, OpId consumer) {
    Require(value.value < restricted_.size() && !restricted_[value.value], Error::kEscape);
    restricted_[value.value] = consumer;
  }
  void Move(OpId id, std::uint32_t parameter, WorkBlockId block) {
    Require(Op(id).block == block && !moved_[id.value] && !kept_[id.value], Error::kShape);
    moved_[id.value] = parameter;
  }
  void Edge(EdgeId id, WorkBlockId from, WorkBlockId to) const {
    Require(id.value < graph_.edges.size(), Error::kControl);
    const auto& edge = graph_.edges[id.value]; const auto& block = graph_.blocks[from.value];
    Require(edge.source == from && edge.target == to && id.value >= block.first_edge.value &&
        id.value - block.first_edge.value < block.edge_count, Error::kControl);
  }
  bool Literal(const ValueOperand& input, const ValueOperand& wanted, OpId consumer,
      std::uint32_t parameter, WorkBlockId block) {
    if (!std::holds_alternative<ValueId>(input)) return Equal(input, wanted);
    const auto id = std::get<ValueId>(input);
    Require(id.value < graph_.values.size(), Error::kLiteral);
    const auto* result = std::get_if<OpResult>(&graph_.values[id.value].origin);
    if (!result) return false;
    const auto& op = Op(result->op);
    if (!std::holds_alternative<ValueCopy>(op.operation) || op.use_count != 1 || op.result != id || op.block != block) return false;
    if (!Equal(Operand(result->op, 0, OperandRole::kInput), wanted)) return false;
    Pure(op); Move(result->op, parameter, block); Restrict(id, consumer); return true;
  }
  void HeaderStore(OpId id) const {
    const auto& op = Op(id);
    Require(op.use_count == 1 && !op.result && id.value < cells_.operations.size() && cells_.operations[id.value], Error::kCell);
    const auto cell = *cells_.operations[id.value];
    Require(cell.value < cells_.cells.size(), Error::kCell);
    const auto& storage = cells_.cells[cell.value];
    Require(std::holds_alternative<AllocatedCell>(storage.storage) && storage.count &&
        storage.first < cells_.accesses.size() && cells_.accesses[storage.first].operation == id, Error::kCell);
    const auto& value = Operand(id, 0, OperandRole::kStoredValue);
    const auto* entry = std::get_if<ValueId>(&value);
    Require(entry && entry->value < graph_.values.size(), Error::kParameter);
    const auto* definition = std::get_if<EntryDefinition>(&graph_.values[entry->value].origin);
    Require(definition && (std::holds_alternative<ParameterEntry>(*definition) ||
        std::holds_alternative<ReceiverEntry>(*definition)), Error::kParameter);
  }
  void Candidate(std::size_t i) {
    const auto& c = candidates_.candidates[i];
    Require(c.header.value < prefix_.size() && c.arm.value < prefix_.size() && c.join.value < prefix_.size() &&
        c.header != c.arm && c.header != c.join && c.arm != c.join &&
        !prefix_[c.header.value] && !prefix_[c.arm.value], Error::kControl);
    Require(c.header == (i ? candidates_.candidates[i - 1].join : WorkBlockId{0}) &&
        predecessors_[c.arm.value] == 1 && predecessors_[c.join.value] == 2, Error::kControl);
    Require(c.parameter < parameters_.formals.size() && parameters_.formals[c.parameter].entry == c.entry &&
        (!i || c.parameter > candidates_.candidates[i - 1].parameter), Error::kParameter);
    Require(c.entry.value < graph_.values.size(), Error::kParameter);
    const auto* entry = std::get_if<EntryDefinition>(&graph_.values[c.entry.value].origin);
    const auto* formal = entry ? std::get_if<ParameterEntry>(entry) : nullptr;
    Require(formal && formal->index == c.parameter, Error::kParameter);
    prefix_[c.header.value] = PrefixBlock{c.parameter, PrefixKind::kHeader};
    prefix_[c.arm.value] = PrefixBlock{c.parameter, PrefixKind::kArm};
    Require(graph_.blocks[c.header.value].edge_count == 2 && graph_.blocks[c.arm.value].edge_count == 1 &&
        c.present != c.missing, Error::kControl);
    Edge(c.present, c.header, c.join); Edge(c.missing, c.header, c.arm); Edge(c.merge, c.arm, c.join);
    Require(c.cell.value < cells_.cells.size() && !candidate_cells_[c.cell.value], Error::kCell);
    candidate_cells_[c.cell.value] = true;
    HeaderStore(c.initial_store);
    const auto& initial = Op(c.initial_store);
    Require(initial.block.value < prefix_.size() && prefix_[initial.block.value] &&
        prefix_[initial.block.value]->kind == PrefixKind::kHeader &&
        (initial.block != c.header || initial.order < Op(c.test_load).order), Error::kCell);
    Require(cells_.operations[c.initial_store.value] == c.cell && !moved_[c.initial_store.value] &&
        !kept_[c.initial_store.value] && Equal(Operand(c.initial_store, 0, OperandRole::kStoredValue), c.entry), Error::kCell);
    kept_[c.initial_store.value] = true; Restrict(c.entry, c.initial_store);
    const auto& load = Op(c.test_load);
    Require(std::holds_alternative<ValueLexicalLoad>(load.operation) && load.result && load.use_count == 0 &&
        cells_.operations[c.test_load.value] == c.cell, Error::kCell);
    Move(c.test_load, c.parameter, c.header); Restrict(*load.result, c.comparison);
    const auto& comparison = Op(c.comparison);
    const auto* binary = std::get_if<ValueBinary>(&comparison.operation);
    Require(binary && comparison.result && comparison.use_count == 2 &&
        (binary->op == BinaryOperator::kStrictEqual || binary->op == BinaryOperator::kStrictNotEqual), Error::kShape);
    Pure(comparison);
    const auto& left = Operand(c.comparison, 0, OperandRole::kLeft);
    const auto& right = Operand(c.comparison, 1, OperandRole::kRight);
    Require((Equal(left, *load.result) && Literal(right, SpecialValue::kUndefined, c.comparison, c.parameter, c.header)) ||
        (Equal(right, *load.result) && Literal(left, SpecialValue::kUndefined, c.comparison, c.parameter, c.header)), Error::kLiteral);
    Move(c.comparison, c.parameter, c.header); Restrict(*comparison.result, c.branch);
    const auto& branch = Op(c.branch); const auto* condition = std::get_if<ValueBranch>(&branch.operation);
    Require(condition && branch.use_count == 1 && !branch.result &&
        Equal(Operand(c.branch, 0, OperandRole::kCondition), *comparison.result), Error::kShape);
    const bool positive = condition->predicate == UnaryOperator::kTrueOrNonzeroNumber;
    Require(positive || condition->predicate == UnaryOperator::kFalseOrZero, Error::kControl);
    const bool missing_taken = (binary->op == BinaryOperator::kStrictEqual) == positive;
    Require(graph_.edges[c.missing.value].kind == (missing_taken ? FlowEdgeKind::kTaken : FlowEdgeKind::kFallthrough) &&
        graph_.edges[c.present.value].kind == (missing_taken ? FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken), Error::kControl);
    Pure(branch); Move(c.branch, c.parameter, c.header);
    const auto& store = Op(c.fallback_store);
    Require(std::holds_alternative<ValueLexicalStore>(store.operation) && store.use_count == 1 && !store.result &&
        cells_.operations[c.fallback_store.value] == c.cell && !std::holds_alternative<ValueId>(c.fallback), Error::kCell);
    Require(Literal(Operand(c.fallback_store, 0, OperandRole::kStoredValue), c.fallback, c.fallback_store, c.parameter, c.arm), Error::kLiteral);
    Move(c.fallback_store, c.parameter, c.arm);
  }
  const ValueProgram& graph_;
  const CellPlan& cells_;
  const ParameterPlan& parameters_;
  const CellDefaults& candidates_;
  SemanticBudget initial_, left_;
  std::vector<std::optional<PrefixBlock>> prefix_;
  std::vector<std::size_t> predecessors_;
  std::vector<std::optional<std::uint32_t>> moved_;
  std::vector<bool> kept_, candidate_cells_;
  std::vector<std::optional<OpId>> restricted_;
};
}  // namespace
std::variant<CellDefaultProof, CellDefaultProofError> CellDefaultProof::Build(
    const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ParameterPlan& parameters, const CellDefaults& candidates,
    std::uint32_t length, analysis::SemanticBudget budget) {
  try {
    constexpr std::size_t kStateBytes = sizeof(CellDefaultProof) + sizeof(Checker);
    Require(kStateBytes <= budget.bytes, Error::kResourceLimit);
    budget.bytes -= kStateBytes;
    const auto checked = VerifyCellPlan(graph, environments, cells, budget);
    if (const auto* error = std::get_if<CellError>(&checked))
      return *error == CellError::kResourceLimit ? Error::kResourceLimit :
          *error == CellError::kWorkLimit ? Error::kWorkLimit : Error::kCell;
    const auto work = std::get<std::uint64_t>(checked);
    Require(work <= budget.work, Error::kWorkLimit); budget.work -= work;
    Checker checker(graph, cells, parameters, candidates, budget);
    CellDefaultProof result;
    result.operations_ = checker.Check(length);
    const auto remaining = checker.Remaining();
    Require(candidates.candidates.size() <= remaining.bytes / sizeof(CellDefault), Error::kResourceLimit);
    result.defaults_ = candidates.candidates;
    Require(result.defaults_.capacity() <= remaining.bytes / sizeof(CellDefault), Error::kResourceLimit);
    result.revision_ = graph.revision; result.cfg_revision_ = graph.cfg_revision;
    result.body_ = candidates.candidates.back().join;
    result.allocated_bytes_ = kStateBytes + budget.bytes - remaining.bytes + result.defaults_.capacity() * sizeof(CellDefault);
    Require(candidates.candidates.size() <= remaining.work, Error::kWorkLimit);
    result.work_ = work + checker.Work() + candidates.candidates.size();
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
