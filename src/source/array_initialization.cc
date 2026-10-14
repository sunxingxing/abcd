#include "array_initialization.h"
#include "effect_order.h"
#include "phase_corridor.h"
#include "evaluation_liveness.h"
#include "../analysis/value_phi.h"
#include "../analysis/value_edits.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <charconv>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = ArrayInitializationError;
void Require(bool ok, Error error) { if (!ok) throw error; }
class Budget {
 public:
  explicit Budget(SemanticBudget quota) : initial_(quota), left_(quota) {}
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t n, std::size_t width = 1) {
    Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width;
  }
  template<class T> void Array(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T)); Step(n); v.resize(n); Admit(v.capacity() - n, sizeof(T));
  }
  SemanticBudget Remaining() const { return left_; }
  std::size_t Bytes() const { return initial_.bytes - left_.bytes; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  SemanticBudget initial_, left_;
};
ArrayInitializationMetadata Metadata(const ValueProgram& graph, ExpressionConstants constants,
    std::span<const std::u16string> names, Budget& budget) {
  Require(constants.operations.size() == graph.ops.size(), Error::kCorrespondence);
  ArrayInitializationMetadata result;
  budget.Array(result.buffers, graph.ops.size()); budget.Array(result.length_reads, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto& op = graph.ops[i]; if (op.life != ValueLife::kLive) continue;
    if (const auto* allocation = std::get_if<ValueArrayAllocation>(&op.operation); allocation && allocation->literal) {
      const auto* buffer = std::get_if<AggregateConstant>(&constants.operations[i]);
      Require(buffer && buffer->kind == AggregateKind::kArray && buffer->first <= constants.properties.size() &&
          buffer->count <= constants.properties.size() - buffer->first, Error::kDefinition);
      // The pool adapter materializes a dense array prefix, not object keys.
      // Recheck its textual boundary here; index identity stays numeric below.
      for (std::uint32_t p = 0; p < buffer->count; ++p) {
        budget.Step(); const auto key = constants.properties[buffer->first + p].key;
        Require(key.value < names.size(), Error::kDefinition);
        constexpr auto kIndexDigits = std::numeric_limits<std::uint32_t>::digits10 + 1;
        char digits[kIndexDigits]; const auto text = std::to_chars(digits, digits + kIndexDigits, p);
        Require(text.ec == std::errc{} && names[key.value].size() == static_cast<std::size_t>(text.ptr - digits), Error::kDefinition);
        for (std::size_t n = 0; n < names[key.value].size(); ++n) {
          budget.Step(); Require(names[key.value][n] == static_cast<char16_t>(digits[n]), Error::kDefinition);
        }
      }
      result.buffers[i] = buffer->count;
    }
    if (const auto* read = std::get_if<ValueNamedRead>(&op.operation); read && read->lookup == PropertyLookup::kObject) {
      const auto* name = std::get_if<NameId>(&constants.operations[i]);
      result.length_reads[i] = name && name->value < names.size() && names[name->value] == u"length";
    }
  }
  return result;
}
class Construction {
 public:
  Construction(const ValueProgram& graph, SourceContract contract, const ArrayInitializationMetadata& metadata,
      Budget& budget, const EvaluationPlan* phases)
      : g_(graph), contract_(contract), metadata_(metadata), b_(budget), evaluation_(phases) {}
  ArrayInitializationPlan Run(const ArrayInitializationPlan* supplied) {
    Require(ArraySpreads(contract_) == ArraySpreadSemantics::kSourceIterator &&
        ResourceFailures(contract_) == ResourceFailureScope::kExcluded, Error::kContract);
    Require(metadata_.buffers.size() == g_.ops.size() && metadata_.length_reads.size() == g_.ops.size(), Error::kCorrespondence);
    if (evaluation_ && !g_.phases.empty()) {
      const auto live = VerifyElidedPureEvaluations(g_, *evaluation_, b_.Remaining());
      if (const auto* error = std::get_if<EvaluationFailure>(&live))
        throw *error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
            *error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kCorrespondence;
      b_.Step(std::get<std::uint64_t>(live));
    }
    auto flow = BuildEffectOrder(g_, b_.Remaining());
    if (const auto* error = std::get_if<ValueAvailabilityError>(&flow))
      throw *error == ValueAvailabilityError::kResourceLimit ? Error::kResourceLimit :
          *error == ValueAvailabilityError::kWorkLimit ? Error::kWorkLimit : Error::kCorrespondence;
    effects_ = std::get<EffectOrder>(std::move(flow)); b_.Admit(effects_.allocated_bytes); b_.Step(effects_.work);
    auto identities = AnalyzeValueIdentities(g_, b_.Remaining());
    if (const auto* error = std::get_if<ValueSimplifyError>(&identities))
      throw *error == ValueSimplifyError::kResourceLimit ? Error::kResourceLimit :
          *error == ValueSimplifyError::kWorkLimit ? Error::kWorkLimit : Error::kCorrespondence;
    identity_ = std::get<ValueIdentities>(std::move(identities)); b_.Admit(identity_.allocated_bytes); b_.Step(identity_.work);
    ArrayInitializationPlan result{g_.revision, g_.cfg_revision, contract_};
    b_.Array(result.metadata.buffers, g_.ops.size()); b_.Array(result.metadata.length_reads, g_.ops.size());
    std::copy(metadata_.buffers.begin(), metadata_.buffers.end(), result.metadata.buffers.begin());
    std::copy(metadata_.length_reads.begin(), metadata_.length_reads.end(), result.metadata.length_reads.begin());
    b_.Array(result.owners, g_.ops.size());
    b_.Array(arrays_, g_.ops.size()); b_.Array(cursor_owners_, g_.values.size());
    b_.Array(cursors_, g_.values.size()); b_.Array(finals_, g_.ops.size());
    // Identify only arrays that actually contain spread. Ordinary literals keep
    // their existing plan and exact spelling. No sample/profile dispatch here.
    for (std::size_t i = 0; i < g_.ops.size(); ++i) {
      b_.Step(); const auto& op = g_.ops[i];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueArraySpread>(op.operation)) continue;
      Require(op.use_count == 3 && op.result.has_value(), Error::kDefinition);
      Role(op, 0, OperandRole::kDestination); Role(op, 1, OperandRole::kIndex); Role(op, 2, OperandRole::kSource);
      const auto allocation = Destination(Input(op, 0)); arrays_[allocation.value] = 1;
    }
    std::vector<OpId> pending; b_.Array(pending, g_.ops.size()); std::size_t count = 0;
    for (std::size_t i = 0; i < g_.ops.size(); ++i) {
      b_.Step(); const auto& op = g_.ops[i]; if (op.life != ValueLife::kLive) continue;
      const OpId id{static_cast<std::uint32_t>(i)};
      const bool spread = std::holds_alternative<ValueArraySpread>(op.operation);
      const bool indexed = std::holds_alternative<ValueIndexedWrite>(op.operation);
      const bool computed = std::holds_alternative<ValueComputedOwnWrite>(op.operation);
      const bool length = Length(id);
      if (spread || indexed || computed || length) {
        const auto owner = Allocation(Input(op, 0));
        if (owner && arrays_[owner->value]) {
          result.owners[i] = *owner;
          if (spread || length) {
            Require(op.result.has_value(), Error::kDefinition);
            cursor_owners_[Canonical(*op.result).value] = *owner; pending[count++] = id;
          }
        } else if (spread) throw Error::kDestination;
      }
    }
    // Index unary cursor dependencies once. Multiple advances are retained in
    // a use adjacency list below; no per-cell or per-array scans.
    std::vector<std::optional<OpId>> head, next;
    b_.Array(head, g_.values.size()); b_.Array(next, g_.ops.size());
    for (std::size_t i = 0; i < g_.ops.size(); ++i) {
      b_.Step(); const auto& op = g_.ops[i]; const auto* unary = std::get_if<ValueUnary>(&op.operation);
      if (op.life != ValueLife::kLive || !unary || unary->op != UnaryOperator::kIncrement || op.use_count != 1 || !op.result) continue;
      if (const auto* input = std::get_if<ValueId>(&Input(op, 0))) {
        const auto value = Canonical(*input); next[i] = head[value.value]; head[value.value] = OpId{static_cast<std::uint32_t>(i)};
      }
    }
    for (std::size_t at = 0; at < count; ++at) {
      b_.Step(); const auto producer = pending[at]; const auto value = Canonical(*g_.ops[producer.value].result);
      for (auto child = head[value.value]; child; child = next[child->value]) {
        b_.Step(); Require(!result.owners[child->value], Error::kCursor);
        result.owners[child->value] = cursor_owners_[value.value];
        cursor_owners_[Canonical(*g_.ops[child->value].result).value] = cursor_owners_[value.value];
        Require(count < pending.size(), Error::kCursor); pending[count++] = *child;
      }
    }
    Operands(result);
    std::vector<ExecutionAnchor> anchors; b_.Array(anchors, g_.ops.size());
    std::size_t steps = 0, arrays = 0;
    for (std::size_t i = 0; i < g_.ops.size(); ++i) {
      b_.Step(); if (arrays_[i]) { ++arrays; anchors[i] = ExecutionAnchor::kRequired; }
      if (result.owners[i]) { ++steps; anchors[i] = ExecutionAnchor::kRequired; }
      if (result.nested[i] || guard_reads_[i]) anchors[i] = ExecutionAnchor::kRequired;
    }
    // Required anchors prove pure cursor calculations also execute once. Loops
    // and conditional omissions fail these same must-reach equations.
    auto ordered = BuildEffectOrder(g_, b_.Remaining(), {}, anchors);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&ordered))
      throw *error == ValueAvailabilityError::kResourceLimit ? Error::kResourceLimit :
          *error == ValueAvailabilityError::kWorkLimit ? Error::kWorkLimit : Error::kOrder;
    b_.Admit(std::get<EffectOrder>(ordered).allocated_bytes); b_.Step(std::get<EffectOrder>(ordered).work);
    effects_ = std::get<EffectOrder>(std::move(ordered));
    std::vector<std::uint32_t> counts; b_.Array(counts, g_.ops.size());
    for (const auto owner : result.owners) { b_.Step(); if (owner) ++counts[owner->value]; }
    b_.Array(result.arrays, arrays); b_.Array(result.steps, steps);
    std::size_t group = 0, step = 0;
    std::optional<PhaseBudget> phase_budget;
    std::optional<PhaseCorridors> corridors;
    if (!g_.phases.empty()) {
      Require(evaluation_ != nullptr, Error::kCatch); phase_budget.emplace(b_.Remaining());
      corridors.emplace(g_, *evaluation_, *phase_budget, PhaseLiteralIntervals::kExclude);
    }
    for (std::size_t i = 0; i < arrays_.size(); ++i) {
      b_.Step(); if (!arrays_[i]) continue;
      const OpId allocation{static_cast<std::uint32_t>(i)};
      const auto prefix = Prefix(allocation);
      result.arrays[group++] = {allocation, static_cast<std::uint32_t>(step), counts[i], prefix};
      ArrayCursor current{{}, prefix}; auto previous = allocation;
      std::optional<OpId> throwing;
      for (std::uint32_t n = 0; n < counts[i]; ++n) {
        b_.Step(); auto next = effects_.after[previous.value];
        // Skip only independently owned RHS events to locate the next commit.
        // Consume() below checks their exact expression postorder, including guards.
        while (next && (result.nested[next->value] || guard_reads_[next->value])) {
          b_.Step(); Require(operand_arrays_[next->value] == allocation, Error::kOrder);
          next = effects_.after[next->value];
          Require(++walked_ <= g_.ops.size(), Error::kOrder);
        }
        Require(next && result.owners[next->value] == allocation, Error::kOrder);
        const auto& op = g_.ops[next->value];
        ArrayInitializationStep item{*next, ArrayStepKind::kElement, current, current, {}};
        if (std::holds_alternative<ValueArraySpread>(op.operation)) {
          Require(Cursor(Input(op, 1)) == current, Error::kCursor);
          item.kind = ArrayStepKind::kSpread; item.value = UseId{op.first_use.value + 2};
          current = {*next, 0}; cursors_[Canonical(*op.result).value] = current;
        } else if (Length(*next)) {
          Require(op.use_count == 1 && op.result, Error::kDefinition);
          Role(op, 0, OperandRole::kBase); item.kind = ArrayStepKind::kLength;
          cursors_[Canonical(*op.result).value] = current;
        } else if (const auto* unary = std::get_if<ValueUnary>(&op.operation)) {
          Require(unary->op == UnaryOperator::kIncrement && op.use_count == 1 && op.result, Error::kCursor);
          item.kind = ArrayStepKind::kAdvance;
          auto advanced = Cursor(Input(op, 0)); Require(advanced.offset < UINT64_MAX, Error::kCursor);
          ++advanced.offset; cursors_[Canonical(*op.result).value] = advanced;
        } else {
          const auto* indexed = std::get_if<ValueIndexedWrite>(&op.operation);
          Require(indexed || std::holds_alternative<ValueComputedOwnWrite>(op.operation), Error::kDefinition);
          Require(!indexed || indexed->kind == IndexedDefinition::kOwnStore, Error::kDefinition);
          Require(!op.result && op.use_count == (indexed ? 2U : 3U), Error::kDefinition);
          Role(op, 0, OperandRole::kBase);
          const auto index = indexed ? ArrayCursor{{}, indexed->index} : Cursor(Input(op, 1));
          Require(index == current, Error::kCursor);
          if (!indexed) Role(op, 1, OperandRole::kKey);
          item.value = UseId{op.first_use.value + (indexed ? 1U : 2U)};
          Require(g_.uses[item.value->value].role == OperandRole::kStoredValue, Error::kDefinition);
          Require(current.offset < UINT64_MAX, Error::kCursor); ++current.offset;
        }
        item.after = current;
        if (item.value) Consume(*item.value, result, previous, throwing, corridors ? &*corridors : nullptr);
        Event(*next, previous, throwing, corridors ? &*corridors : nullptr);
        result.steps[step++] = item; previous = *next;
      }
      finals_[i] = previous;
    }
    if (phase_budget) { b_.Admit(phase_budget->Cost().bytes); b_.Step(phase_budget->Cost().work); }
    Census(result);
    if (supplied) {
      b_.Step(result.arrays.size() + result.steps.size() + result.owners.size() +
          result.nested.size() + result.guards.size());
      Require(supplied->revision == g_.revision && supplied->cfg_revision == g_.cfg_revision &&
          supplied->contract == contract_ && supplied->arrays == result.arrays &&
          supplied->steps == result.steps && supplied->owners == result.owners &&
          supplied->nested == result.nested && supplied->guards == result.guards, Error::kCorrespondence);
    }
    result.allocated_bytes = sizeof(result) + result.arrays.capacity() * sizeof(ArrayInitialization) +
        result.steps.capacity() * sizeof(ArrayInitializationStep) + result.owners.capacity() * sizeof(std::optional<OpId>) + result.metadata.buffers.capacity() * sizeof(std::optional<std::uint32_t>) + result.metadata.length_reads.capacity();
    result.allocated_bytes += result.nested.capacity() * sizeof(std::optional<UseId>) +
        result.guards.capacity() * sizeof(std::optional<OpId>);
    result.peak_bytes = b_.Bytes(); result.work = b_.Work(); return result;
  }
 private:
  ValueId Canonical(ValueId value) const { return evaluation_ ? ResolveEvaluationValue(*evaluation_, value) : identity_.canonical[value.value]; }
  const ValueOperand& Input(const ValueOp& op, std::uint32_t i) const {
    Require(i < op.use_count && std::size_t(op.first_use.value) + i < g_.uses.size(), Error::kDefinition);
    return g_.uses[op.first_use.value + i].operand;
  }
  void Role(const ValueOp& op, std::uint32_t i, OperandRole role) const {
    (void)Input(op, i); Require(g_.uses[op.first_use.value + i].role == role &&
        g_.uses[op.first_use.value + i].purpose == ReadPurpose::kOperand, Error::kDefinition);
  }
  std::optional<OpId> Allocation(const ValueOperand& input) const {
    const auto* value = std::get_if<ValueId>(&input); if (!value) return {};
    const auto canonical = Canonical(*value);
    const auto* origin = std::get_if<OpResult>(&g_.values[canonical.value].origin);
    if (!origin) return {};
    const auto& op = g_.ops[origin->op.value];
    if (op.life != ValueLife::kLive || !std::holds_alternative<ValueArrayAllocation>(op.operation) ||
        op.result != canonical || op.use_count) return {};
    return origin->op;
  }
  OpId Destination(const ValueOperand& input) const {
    const auto allocation = Allocation(input); Require(allocation.has_value(), Error::kDestination); return *allocation;
  }
  bool Length(OpId id) const {
    const auto& op = g_.ops[id.value]; if (!std::holds_alternative<ValueNamedRead>(op.operation)) return false;
    Require(metadata_.length_reads[id.value] <= 1, Error::kCorrespondence);
    return metadata_.length_reads[id.value];
  }
  std::uint32_t Prefix(OpId id) const {
    const auto& allocation = std::get<ValueArrayAllocation>(g_.ops[id.value].operation);
    if (!allocation.literal) return 0;
    Require(metadata_.buffers[id.value].has_value(), Error::kDefinition);
    return *metadata_.buffers[id.value];
  }
  ArrayCursor Cursor(const ValueOperand& operand) const {
    const ValueOperand* input = &operand;
    if (const auto* id = std::get_if<ValueId>(input)) {
      const auto value = Canonical(*id);
      if (cursors_[value.value]) return *cursors_[value.value];
      const auto* origin = std::get_if<OpResult>(&g_.values[value.value].origin);
      Require(origin && std::holds_alternative<ValueCopy>(g_.ops[origin->op.value].operation), Error::kCursor);
      input = &Input(g_.ops[origin->op.value], 0);
    }
    if (const auto* integer = std::get_if<analysis::IntegerLiteral>(input)) {
      Require(integer->value >= 0, Error::kCursor); return {{}, static_cast<std::uint64_t>(integer->value)};
    }
    if (const auto* bits = std::get_if<Float64Bits>(input)) {
      const auto value = std::bit_cast<double>(bits->value);
      Require(std::isfinite(value) && value >= 0 && value <= UINT32_MAX && std::floor(value) == value, Error::kCursor);
      return {{}, static_cast<std::uint64_t>(value)};
    }
    throw Error::kCursor;
  }
  bool Early(const ValueOperand& input, OpId allocation) const {
    if (const auto* id = std::get_if<ValueId>(&input)) {
      const auto value = Canonical(*id); const auto& at = g_.ops[allocation.value];
      const auto* origin = std::get_if<OpResult>(&g_.values[value.value].origin);
      if (origin && std::holds_alternative<ValueCopy>(g_.ops[origin->op.value].operation) &&
          !std::holds_alternative<ValueId>(Input(g_.ops[origin->op.value], 0))) return true;
      return g_.values[value.value].domain.IsLanguage() &&
          ValueAvailableAt(g_, effects_.dominators, value, at.block, at.order);
    }
    return true;
  }
  bool Elided(OpId id) const { return evaluation_ && !g_.phases.empty() && evaluation_->operations[id.value].kind == EvaluationKind::kElidedPure; }
  bool Receiver(UseId use) const {
    return evaluation_ && IsMemberReceiver(g_, *evaluation_, g_.uses[use.value]);
  }
  void Operands(ArrayInitializationPlan& plan) {
    b_.Array(plan.nested, g_.ops.size()); b_.Array(plan.guards, g_.ops.size());
    b_.Array(guard_reads_, g_.ops.size()); b_.Array(operand_arrays_, g_.ops.size());
    b_.Array(stack_, g_.ops.size() + g_.uses.size());
    if (evaluation_) for (const auto& module : evaluation_->modules) {
      b_.Step(); plan.guards[module.pair.read.value] = module.pair.guard;
    }
    // Discover only values unavailable at allocation. Ownership is one-use,
    // independent of physical blocks, register names, and instruction order.
    std::vector<OpId> pending; b_.Array(pending, g_.ops.size());
    std::size_t count = 0;
    const auto own = [&](UseId use, OpId array) {
      b_.Step(); const auto& input = g_.uses[use.value].operand;
      if (Early(input, array)) return;
      const auto* value = std::get_if<ValueId>(&input);
      Require(value != nullptr, Error::kOperand);
      const auto canonical = Canonical(*value);
      const auto* origin = std::get_if<OpResult>(&g_.values[canonical.value].origin);
      Require(origin && origin->op != array && !plan.owners[origin->op.value], Error::kOperand);
      const auto id = origin->op; const auto& op = g_.ops[id.value];
      const auto shape = DescribeEvaluation(op.operation);
      Require(shape && shape->expression && shape->inputs == InputEvaluation::kEager &&
          shape->Count() == op.use_count && op.result && op.life == ValueLife::kLive &&
          (g_.values[canonical.value].domain.IsLanguage() || plan.guards[id.value]) &&
          effects_.Before(g_, array, op.block, op.order), Error::kOperand);
      switch (shape->source) {
        case SourceEvaluation::kPure: Require(CanErasePureOp(op), Error::kOperand); break;
        case SourceEvaluation::kDirect: case SourceEvaluation::kSnapshotInputs: break;
        case SourceEvaluation::kInvocation: case SourceEvaluation::kSuspending:
        case SourceEvaluation::kUnsupported: throw Error::kOperand;
      }
      Require(!plan.nested[id.value], Error::kSharedOperand);
      plan.nested[id.value] = use; operand_arrays_[id.value] = array;
      if (plan.guards[id.value]) {
        const auto guard = *plan.guards[id.value];
        Require(!guard_reads_[guard.value], Error::kOperand);
        guard_reads_[guard.value] = id; operand_arrays_[guard.value] = array;
      }
      Require(count < pending.size(), Error::kOperand); pending[count++] = id;
    };
    for (std::size_t i = 0; i < g_.ops.size(); ++i) {
      b_.Step(); if (!plan.owners[i]) continue;
      const auto& op = g_.ops[i];
      if (std::holds_alternative<ValueArraySpread>(op.operation)) own({op.first_use.value + 2}, *plan.owners[i]);
      else if (std::holds_alternative<ValueIndexedWrite>(op.operation)) own({op.first_use.value + 1}, *plan.owners[i]);
      else if (std::holds_alternative<ValueComputedOwnWrite>(op.operation)) own({op.first_use.value + 2}, *plan.owners[i]);
    }
    for (std::size_t at = 0; at < count; ++at) {
      b_.Step(); const auto id = pending[at]; const auto& op = g_.ops[id.value];
      const auto shape = *DescribeEvaluation(op.operation);
      for (std::uint32_t n = 0; n < op.use_count; ++n) {
        const UseId use{op.first_use.value + n}; b_.Step();
        Require(!CheckEvaluationUse(g_, g_.uses[use.value], shape, n), Error::kOperand);
        if (Receiver(use)) continue;
        Require((!shape.NeedsSnapshot(g_.uses[use.value].role) &&
            !PredicateSnapshot(g_, op, g_.uses[use.value].operand)) ||
            Early(g_.uses[use.value].operand, *operand_arrays_[id.value]), Error::kOperand);
        own(use, *operand_arrays_[id.value]);
      }
    }
    // Original canonical use chains include catch/phi observations. Never
    // duplicate a getter/call or silently absorb storage seen by a handler.
    for (std::size_t u = 0; u < g_.uses.size(); ++u) {
      b_.Step(); const auto& use = g_.uses[u]; if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand); if (!value) continue;
      const auto canonical = Canonical(*value);
      const auto* origin = std::get_if<OpResult>(&g_.values[canonical.value].origin);
      if (!origin || !plan.nested[origin->op.value]) continue;
      const auto* user = std::get_if<OpId>(&use.owner);
      if (user) {
        const auto& op = g_.ops[user->value]; if (op.life != ValueLife::kLive || Elided(*user)) continue;
        if (op.result && std::holds_alternative<ValueCopy>(op.operation) && Canonical(*op.result) == canonical) continue;
        if (plan.guards[origin->op.value] == *user) continue;
        if (plan.nested[user->value] && Receiver({static_cast<std::uint32_t>(u)})) continue;
      } else if (const auto* phi = std::get_if<ValueId>(&use.owner); phi && Canonical(*phi) == canonical) continue;
      Require(*plan.nested[origin->op.value] == UseId{static_cast<std::uint32_t>(u)}, Error::kSharedOperand);
    }
    // Store only actually consumed guard peers in the certificate.
    for (std::size_t i = 0; i < plan.guards.size(); ++i) { b_.Step(); if (!plan.nested[i]) plan.guards[i].reset(); }
  }
  void Event(OpId id, OpId& previous, std::optional<OpId>& throwing, PhaseCorridors* corridors) {
    b_.Step(); Require(effects_.after[previous.value] == id &&
        effects_.before[id.value] == EffectPredecessor{previous}, Error::kOrder);
    if (corridors && g_.ops[id.value].exceptional == ExceptionalCompletion::kMayThrow) {
      Require(!corridors->HasCatchCopies(id), Error::kCatchStaging);
      if (throwing) Require(corridors->SameCatch(*throwing, id), Error::kCatch);
      else throwing = id;
    }
    previous = id;
  }
  void Consume(UseId use, const ArrayInitializationPlan& plan, OpId& previous,
      std::optional<OpId>& throwing, PhaseCorridors* corridors) {
    std::size_t size = 0;
    const auto push = [&](UseId input) {
      const auto* value = std::get_if<ValueId>(&g_.uses[input.value].operand); if (!value) return;
      const auto* origin = std::get_if<OpResult>(&g_.values[Canonical(*value).value].origin);
      if (!origin || plan.nested[origin->op.value] != input) return;
      Require(size < stack_.size(), Error::kResourceLimit); stack_[size++] = {origin->op, false};
    };
    push(use);
    while (size) {
      b_.Step(); const auto item = stack_[--size]; const auto id = item.operation;
      if (item.finish) {
        Event(id, previous, throwing, corridors);
        if (plan.guards[id.value]) Event(*plan.guards[id.value], previous, throwing, corridors);
      } else {
        Require(size < stack_.size(), Error::kResourceLimit); stack_[size++] = {id, true};
        const auto& op = g_.ops[id.value];
        for (std::uint32_t n = op.use_count; n-- > 0;) {
          b_.Step(); const UseId input{op.first_use.value + n}; if (!Receiver(input)) push(input);
        }
      }
    }
  }
  void Census(const ArrayInitializationPlan& plan) {
    for (std::size_t u = 0; u < g_.uses.size(); ++u) {
      b_.Step(); const auto& use = g_.uses[u]; if (use.life != ValueLife::kLive) continue;
      const auto* input = std::get_if<ValueId>(&use.operand); if (!input) continue;
      const auto value = Canonical(*input); const auto array = Allocation(use.operand);
      const auto cursor = cursor_owners_[value.value];
      if ((!array || !arrays_[array->value]) && !cursor) continue;
      const auto* operation = std::get_if<OpId>(&use.owner);
      if (operation) {
        const auto& op = g_.ops[operation->value];
        if (op.life != ValueLife::kLive || Elided(*operation)) continue;
        if (op.result && std::holds_alternative<ValueCopy>(op.operation) && Canonical(*op.result) == value) continue;
        if (cursor) {
          Require(plan.owners[operation->value] == cursor &&
              ((std::holds_alternative<ValueArraySpread>(op.operation) && use.role == OperandRole::kIndex) ||
               (std::holds_alternative<ValueComputedOwnWrite>(op.operation) && use.role == OperandRole::kKey) ||
               (std::holds_alternative<ValueUnary>(op.operation) && use.role == OperandRole::kInput)), Error::kCursor);
          continue;
        }
        if (plan.owners[operation->value] == array && u == op.first_use.value) continue;
        Require(effects_.Before(g_, *finals_[array->value], op.block, op.order), Error::kEscape);
      } else {
        const auto* phi = std::get_if<ValueId>(&use.owner);
        if (phi && Canonical(*phi) == value) continue;
        Require(!cursor, Error::kCursor);
        const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
        Require(edge && g_.edges[edge->value].kind != FlowEdgeKind::kException, Error::kCatch);
        const auto& flow = g_.edges[edge->value];
        Require(effects_.Before(g_, *finals_[array->value], flow.source,
            g_.blocks[flow.source.value].op_count), Error::kEscape);
      }
    }
  }
  const ValueProgram& g_; SourceContract contract_; const ArrayInitializationMetadata& metadata_;
  Budget& b_; const EvaluationPlan* evaluation_;
  ValueIdentities identity_; EffectOrder effects_;
  struct Visit { OpId operation; bool finish; };
  std::vector<Visit> stack_;
  std::vector<std::optional<OpId>> guard_reads_, operand_arrays_;
  std::size_t walked_ = 0;
  std::vector<std::uint8_t> arrays_;
  std::vector<std::optional<OpId>> cursor_owners_, finals_;
  std::vector<std::optional<ArrayCursor>> cursors_;
};
}
std::variant<ArrayInitializationPlan, ArrayInitializationError> BuildArrayInitializations(
    const analysis::ValueProgram& graph, SourceContract contract, ExpressionConstants constants,
    std::span<const std::u16string> names, analysis::SemanticBudget quota, const EvaluationPlan* phases) {
  try { Budget budget(quota);
    Require(ArraySpreads(contract) == ArraySpreadSemantics::kSourceIterator, Error::kContract);
    const auto metadata = Metadata(graph, constants, names, budget);
    return Construction(graph, contract, metadata, budget, phases).Run(nullptr); }
  catch (Error error) { return error; }
  catch (EvaluationError error) { return error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
      error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kCatch; }
  catch (const std::bad_alloc&) { return Error::kResourceLimit; }
  catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, ArrayInitializationError> VerifyArrayInitializations(
    const analysis::ValueProgram& graph, const ArrayInitializationPlan& plan, SourceContract contract,
    ExpressionConstants constants, std::span<const std::u16string> names,
    analysis::SemanticBudget quota, const EvaluationPlan* phases) {
  try { Budget budget(quota);
    Require(ArraySpreads(contract) == ArraySpreadSemantics::kSourceIterator, Error::kContract);
    const auto metadata = Metadata(graph, constants, names, budget);
    Require(metadata == plan.metadata, Error::kCorrespondence);
    (void)Construction(graph, contract, metadata, budget, phases).Run(&plan); return budget.Work(); }
  catch (Error error) { return error; }
  catch (EvaluationError error) { return error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
      error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kCatch; }
  catch (const std::bad_alloc&) { return Error::kResourceLimit; }
  catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, ArrayInitializationError> VerifyArrayInitializationStructure(
    const analysis::ValueProgram& graph, const ArrayInitializationPlan& plan,
    analysis::SemanticBudget quota, const EvaluationPlan* phases) {
  try { Budget budget(quota); (void)Construction(graph, plan.contract, plan.metadata, budget, phases).Run(&plan); return budget.Work(); }
  catch (Error error) { return error; }
  catch (EvaluationError error) { return error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
      error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kCatch; }
  catch (const std::bad_alloc&) { return Error::kResourceLimit; }
  catch (const std::length_error&) { return Error::kResourceLimit; }
}

}
