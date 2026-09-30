#include "default_value_proof.h"

#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = DefaultProofError;
void Require(bool condition, Error error = Error::kShape) { if (!condition) throw error; }
bool SameLiteral(const ValueOperand& left, const ValueOperand& right) {
  if (left.index() != right.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    const auto& other = std::get<T>(right);
    if constexpr (std::is_same_v<T, SpecialValue>) return value == other;
    else return value.value == other.value;
  }, left);
}
enum class BlockRole : std::uint8_t { kHeader, kArm };
struct BlockOwner { std::uint32_t candidate; BlockRole role; };
enum class ValueRole : std::uint8_t { kEntry, kComparison, kInitializer, kMerged, kUndefined, kNumber };
struct RestrictedValue { std::uint32_t candidate; ValueRole role; };
struct PhiCount { bool present = false; bool missing = false; };
class Checker {
 public:
  Checker(const ValueProgram& graph, const ParameterPlan& parameters,
          const DefaultCandidates& candidates, SemanticBudget budget)
      : graph_(graph), parameters_(parameters), candidates_(candidates), initial_(budget), budget_(budget) {}
  std::vector<DefaultOpPlacement> Check(std::uint32_t length) {
    Require(parameters_.revision == graph_.revision && parameters_.cfg_revision == graph_.cfg_revision &&
        candidates_.revision == graph_.revision && candidates_.cfg_revision == graph_.cfg_revision, Error::kRevision);
    Require(!candidates_.values.empty() && length < parameters_.formals.size() &&
        candidates_.values.front().parameter == length, Error::kParameter);
    Require(graph_.ops.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Array(blocks_, graph_.blocks.size());
    Array(predecessors_, graph_.blocks.size());
    Array(values_, graph_.values.size());
    Array(joins_, graph_.blocks.size());
    Array(phi_counts_, candidates_.values.size());
    Array(formals_, parameters_.formals.size());
    for (std::size_t i = 0; i < candidates_.values.size(); ++i) {
      Step(); CheckCandidate(static_cast<std::uint32_t>(i));
    }
    for (const auto& edge : graph_.edges) {
      Step(); Require(edge.source.value < blocks_.size() && edge.target.value < blocks_.size());
      Require(predecessors_[edge.target.value] < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
      ++predecessors_[edge.target.value];
    }
    Require(predecessors_[0] == 0, Error::kControl);
    for (const auto& candidate : candidates_.values) {
      Step(); Require(predecessors_[candidate.arm.value] == 1 &&
          predecessors_[candidate.join.value] == 2, Error::kControl);
    }
    CheckValues();
    std::vector<DefaultOpPlacement> operations;
    // At most every operation is moved; admit the retained copy alongside all
    // verifier scratch. No per-parameter walk over operations or uses.
    Admit(graph_.ops.size(), sizeof(DefaultOpPlacement));
    operations.reserve(graph_.ops.size());
    Admit(operations.capacity() - graph_.ops.size(), sizeof(DefaultOpPlacement));
    std::optional<std::uint16_t> prefix_slots;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); const auto& op = graph_.ops[i];
      Require(op.block.value < blocks_.size());
      if (op.life != ValueLife::kLive || !blocks_[op.block.value]) continue;
      const auto owner = *blocks_[op.block.value];
      const auto& candidate = candidates_.values[owner.candidate];
      const OpId id{static_cast<std::uint32_t>(i)};
      if (id != candidate.comparison && id != candidate.branch && id != candidate.initializer &&
          candidate.undefined != id && candidate.number != id) {
        // Keep fresh environment initialization in the residual body. Literal
        // defaults commute with it only when it stores unaffected invocation
        // entries; CheckUse below still rejects every raw default-entry escape.
        // No closure, caller cell, load or arbitrary effect is admitted here.
        if (owner.candidate == 0 && owner.role == BlockRole::kHeader &&
            op.order < graph_.ops[candidate.comparison.value].order) {
          if (const auto* push = std::get_if<ValueEnvironmentPush>(&op.operation)) {
            Require(!prefix_slots && op.use_count == 0, Error::kEffect);
            prefix_slots = push->slots;
            continue;
          }
          if (const auto* store = std::get_if<ValueLexicalStore>(&op.operation)) {
            Require(prefix_slots && store->address.depth.value == 0 &&
                store->address.slot.value < *prefix_slots && !op.result && op.use_count == 1,
                Error::kEffect);
            const auto* value = std::get_if<ValueId>(&Operand(op, 0, OperandRole::kStoredValue));
            Require(value && value->value < graph_.values.size(), Error::kEffect);
            const auto* entry = std::get_if<EntryDefinition>(&graph_.values[value->value].origin);
            Require(entry && (std::holds_alternative<ParameterEntry>(*entry) ||
                std::holds_alternative<ReceiverEntry>(*entry)), Error::kEffect);
            continue;
          }
        }
        Require(std::holds_alternative<ValueNop>(op.operation) ||
            (owner.role == BlockRole::kArm && std::holds_alternative<ValueJump>(op.operation)), Error::kEffect);
        Require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
            op.exceptional == ExceptionalCompletion::kImpossible && !op.result && op.use_count == 0, Error::kEffect);
      }
      operations.push_back({id, candidate.parameter});
    }
    for (const auto& use : graph_.uses) { Step(); if (use.life == ValueLife::kLive) CheckUse(use); }
    for (const auto count : phi_counts_) { Step(); Require(count.present && count.missing); }
    return operations;
  }
  void Step(std::uint64_t count = 1) { Require(count <= budget_.work, Error::kWorkLimit); budget_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= budget_.bytes / width, Error::kResourceLimit); budget_.bytes -= count * width;
  }
  std::uint64_t Work() const { return initial_.work - budget_.work; }
 private:
  template <class T> void Array(std::vector<T>& array, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T)); Step(count); array.resize(count);
    Admit(array.capacity() - count, sizeof(T));
  }
  const ValueOp& Op(OpId id, WorkBlockId block) {
    Require(id.value < graph_.ops.size()); const auto& op = graph_.ops[id.value];
    Require(op.life == ValueLife::kLive && op.block == block);
    return op;
  }
  const ValueOperand& Operand(const ValueOp& op, std::uint32_t ordinal, OperandRole role) {
    Require(op.first_use.value < graph_.uses.size() && ordinal < op.use_count &&
        ordinal < graph_.uses.size() - op.first_use.value);
    const auto& use = graph_.uses[op.first_use.value + ordinal];
    Require(use.life == ValueLife::kLive && use.role == role && use.purpose == ReadPurpose::kOperand);
    return use.operand;
  }
  void Restrict(ValueId id, std::uint32_t candidate, ValueRole role) {
    Require(id.value < values_.size() && graph_.values[id.value].life == ValueLife::kLive && !values_[id.value]);
    values_[id.value] = RestrictedValue{candidate, role};
  }
  void Edge(EdgeId id, WorkBlockId source, WorkBlockId target) {
    Require(id.value < graph_.edges.size()); const auto& edge = graph_.edges[id.value];
    Require(edge.source == source && edge.target == target, Error::kControl);
    const auto& block = graph_.blocks[source.value];
    Require(id.value >= block.first_edge.value && id.value - block.first_edge.value < block.edge_count, Error::kControl);
  }
  void Pure(const ValueOp& op) {
    Require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
        op.exceptional == ExceptionalCompletion::kImpossible, Error::kEffect);
  }
  void CheckCandidate(std::uint32_t i) {
    const auto& c = candidates_.values[i];
    Require(c.header.value < blocks_.size() && c.arm.value < blocks_.size() && c.join.value < blocks_.size());
    Require(c.header != c.arm && c.header != c.join && c.arm != c.join &&
        !blocks_[c.header.value] && !blocks_[c.arm.value] && !joins_[c.join.value], Error::kControl);
    Require(c.header == (i == 0 ? WorkBlockId{0} : candidates_.values[i - 1].join), Error::kControl);
    Require(c.parameter < parameters_.formals.size() && parameters_.formals[c.parameter].entry == c.entry &&
        (i == 0 || c.parameter > candidates_.values[i - 1].parameter), Error::kParameter);
    formals_[c.parameter] = i;
    blocks_[c.header.value] = BlockOwner{i, BlockRole::kHeader};
    blocks_[c.arm.value] = BlockOwner{i, BlockRole::kArm}; joins_[c.join.value] = i;
    Require(graph_.blocks[c.header.value].edge_count == 2 && graph_.blocks[c.arm.value].edge_count == 1, Error::kControl);
    Require(c.present != c.missing, Error::kControl);
    Edge(c.present, c.header, c.join); Edge(c.missing, c.header, c.arm); Edge(c.merge, c.arm, c.join);
    Restrict(c.entry, i, ValueRole::kEntry); Restrict(c.merged, i, ValueRole::kMerged);
    const auto* entry = std::get_if<EntryDefinition>(&graph_.values[c.entry.value].origin);
    const auto* parameter = entry ? std::get_if<ParameterEntry>(entry) : nullptr;
    Require(parameter && parameter->index == c.parameter, Error::kParameter);
    const auto* phi = std::get_if<PhiDefinition>(&graph_.values[c.merged.value].origin);
    Require(phi && phi->block == c.join);
    const auto& comparison = Op(c.comparison, c.header);
    const auto* binary = std::get_if<ValueBinary>(&comparison.operation);
    Require(binary && (binary->op == BinaryOperator::kStrictEqual || binary->op == BinaryOperator::kStrictNotEqual) &&
        comparison.use_count == 2 && comparison.result);
    Pure(comparison); Restrict(*comparison.result, i, ValueRole::kComparison);
    const auto& left = Operand(comparison, 0, OperandRole::kLeft);
    const auto& right = Operand(comparison, 1, OperandRole::kRight);
    ValueOperand undefined = SpecialValue::kUndefined;
    if (c.undefined) {
      const auto& constant = Op(*c.undefined, c.header);
      Require(std::holds_alternative<ValueCopy>(constant.operation) && constant.result &&
          constant.use_count == 1 && constant.order < comparison.order, Error::kLiteral);
      Pure(constant);
      Require(SameLiteral(Operand(constant, 0, OperandRole::kInput), SpecialValue::kUndefined), Error::kLiteral);
      Restrict(*constant.result, i, ValueRole::kUndefined);
      undefined = *constant.result;
    }
    Require((SameLiteral(left, c.entry) && SameLiteral(right, undefined)) ||
        (SameLiteral(right, c.entry) && SameLiteral(left, undefined)));
    const auto& branch = Op(c.branch, c.header);
    const auto* test = std::get_if<ValueBranch>(&branch.operation);
    Require(test && (test->predicate == UnaryOperator::kTrueOrNonzeroNumber || test->predicate == UnaryOperator::kFalseOrZero) &&
        branch.use_count == 1 && !branch.result && comparison.order < branch.order);
    Pure(branch); Require(SameLiteral(Operand(branch, 0, OperandRole::kCondition), *comparison.result));
    const bool missing_taken = (binary->op == BinaryOperator::kStrictEqual) ==
                               (test->predicate == UnaryOperator::kTrueOrNonzeroNumber);
    Require(graph_.edges[c.missing.value].kind == (missing_taken ? FlowEdgeKind::kTaken : FlowEdgeKind::kFallthrough) &&
        graph_.edges[c.present.value].kind == (missing_taken ? FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken), Error::kControl);
    Require(!std::holds_alternative<ValueId>(c.fallback), Error::kLiteral);
    if (const auto* special = std::get_if<SpecialValue>(&c.fallback))
      Require(*special == SpecialValue::kUndefined || *special == SpecialValue::kNull ||
          *special == SpecialValue::kTrue || *special == SpecialValue::kFalse, Error::kLiteral);
    {
      const auto& initializer = Op(c.initializer, c.arm);
      const auto* unary = std::get_if<ValueUnary>(&initializer.operation);
      Require(initializer.use_count == 1 && initializer.result, Error::kLiteral);
      const auto& input = Operand(initializer, 0, OperandRole::kInput);
      if (std::holds_alternative<ValueCopy>(initializer.operation)) {
        Require(!c.number && !std::holds_alternative<ValueId>(input) && SameLiteral(c.fallback, input), Error::kLiteral);
        Pure(initializer);
      } else {
        const ValueOperand* numeric = &input;
        if (c.number) {
          const auto& constant = Op(*c.number, c.arm);
          Require(std::holds_alternative<ValueCopy>(constant.operation) && constant.result &&
              constant.use_count == 1 && constant.order < initializer.order &&
              SameLiteral(input, *constant.result), Error::kLiteral);
          Pure(constant);
          Restrict(*constant.result, i, ValueRole::kNumber);
          numeric = &Operand(constant, 0, OperandRole::kInput);
        }
        const auto* integer = std::get_if<IntegerLiteral>(numeric);
        Require(unary && unary->op == UnaryOperator::kNegate && integer, Error::kLiteral);
        Require(SameLiteral(c.fallback, Float64Bits{std::bit_cast<std::uint64_t>(-static_cast<double>(integer->value))}), Error::kLiteral);
        // Generic negation over-approximates user coercion. A Number literal
        // proves no callbacks/abrupt completion, including negative zero.
        Require((initializer.normal == LanguageEffect::kNone || initializer.normal == LanguageEffect::kUserCode) &&
            initializer.before_throw == initializer.normal && initializer.exceptional != ExceptionalCompletion::kAlwaysThrow, Error::kEffect);
      }
      Restrict(*initializer.result, i, ValueRole::kInitializer);
    }
  }
  void CheckValues() {
    for (std::size_t i = 0; i < graph_.values.size(); ++i) {
      Step(); const auto& value = graph_.values[i];
      if (value.life != ValueLife::kLive) continue;
      if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
        if (const auto* parameter = std::get_if<ParameterEntry>(entry)) {
          Require(parameter->index < formals_.size(), Error::kParameter);
          if (const auto owner = formals_[parameter->index])
            Require(candidates_.values[*owner].entry.value == i, Error::kParameter);
        }
      }
      if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
        Require(phi->block.value < blocks_.size());
        if (blocks_[phi->block.value] || joins_[phi->block.value])
          Require(values_[i] && values_[i]->role == ValueRole::kMerged, Error::kControl);
      }
    }
  }
  void CheckUse(const OperandUse& use) {
    const auto* op = std::get_if<OpId>(&use.owner);
    const auto* phi = std::get_if<ValueId>(&use.owner);
    if (op) Require(op->value < graph_.ops.size());
    if (phi) Require(phi->value < values_.size());
    if (phi && values_[phi->value] && values_[phi->value]->role == ValueRole::kMerged) {
      const auto i = values_[phi->value]->candidate;
      const auto& c = candidates_.values[i];
      const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
      Require(edge && use.role == OperandRole::kPhiInput && use.purpose == ReadPurpose::kOperand);
      if (*edge == c.present) {
        Require(!phi_counts_[i].present && SameLiteral(use.operand, c.entry)); phi_counts_[i].present = true;
      } else {
        Require(*edge == c.merge && !phi_counts_[i].missing);
        const ValueOperand expected{*graph_.ops[c.initializer.value].result};
        Require(SameLiteral(use.operand, expected), Error::kLiteral); phi_counts_[i].missing = true;
      }
    }
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (!value) return;
    Require(value->value < values_.size());
    if (!values_[value->value]) return;
    const auto restriction = *values_[value->value];
    const auto& c = candidates_.values[restriction.candidate];
    bool allowed = false;
    switch (restriction.role) {
      case ValueRole::kEntry: allowed = (op && *op == c.comparison) || (phi && *phi == c.merged); break;
      case ValueRole::kComparison: allowed = op && *op == c.branch; break;
      case ValueRole::kInitializer: allowed = phi && *phi == c.merged; break;
      case ValueRole::kUndefined: allowed = op && *op == c.comparison; break;
      case ValueRole::kNumber: allowed = op && *op == c.initializer; break;
      case ValueRole::kMerged:
        allowed = op ? !blocks_[graph_.ops[op->value].block.value] : !values_[phi->value]; break;
    }
    bool purpose = use.purpose == ReadPurpose::kOperand;
    if (restriction.role == ValueRole::kMerged && op &&
        use.purpose == ReadPurpose::kProtocol && use.role == OperandRole::kArgument && graph_.arguments) {
      const auto& consumer = graph_.ops[op->value];
      const auto* snapshot = std::get_if<ValueArgumentSnapshot>(&consumer.operation);
      // Only the post-default value may enter this conditional read. Raw entry
      // escapes still reject: native formals no longer expose their old values.
      purpose = snapshot && graph_.arguments->frame.parameters == parameters_.formals.size() &&
          snapshot->slots == graph_.arguments->frame.parameters &&
          consumer.use_count == snapshot->slots;
    }
    Require(allowed && purpose, Error::kEscapedValue);
  }
  const ValueProgram& graph_;
  const ParameterPlan& parameters_;
  const DefaultCandidates& candidates_;
  SemanticBudget initial_;
  SemanticBudget budget_;
  std::vector<std::optional<BlockOwner>> blocks_;
  std::vector<std::uint32_t> predecessors_;
  std::vector<std::optional<RestrictedValue>> values_;
  std::vector<std::optional<std::uint32_t>> joins_;
  std::vector<PhiCount> phi_counts_;
  std::vector<std::optional<std::uint32_t>> formals_;
};
}  // namespace

std::variant<DefaultValueProof, DefaultProofError> DefaultValueProof::Build(
    const analysis::ValueProgram& graph, const ParameterPlan& parameters,
    const DefaultCandidates& candidates, std::uint32_t length,
    analysis::SemanticBudget budget) {
  try {
    Checker checker(graph, parameters, candidates, budget);
    checker.Admit(1, sizeof(DefaultValueProof));
    checker.Admit(candidates.values.size(), sizeof(DefaultCandidate));
    DefaultValueProof proof;
    proof.defaults_ = candidates.values;
    checker.Admit(proof.defaults_.capacity() - candidates.values.size(), sizeof(DefaultCandidate));
    checker.Step(candidates.values.size());
    proof.operations_ = checker.Check(length);
    proof.body_ = proof.defaults_.back().join;
    proof.revision_ = graph.revision; proof.cfg_revision_ = graph.cfg_revision;
    proof.allocated_bytes_ = sizeof(DefaultValueProof) + proof.defaults_.capacity() * sizeof(DefaultCandidate) +
        proof.operations_.capacity() * sizeof(DefaultOpPlacement);
    proof.work_ = checker.Work();
    return proof;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
