#include "literal_plan.h"
#include "evaluation_input_scope.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "../analysis/value_flow.h"
#include "../analysis/value_edits.h"
#include "evaluation_aliases.h"
#include "evaluation_liveness.h"
#include "phase_corridor.h"
#include "effect_order.h"
#include "abcd/analysis/dominators.h"

namespace abcd::source {
namespace {
using Error = LiteralError;
using Guards = std::span<const std::optional<analysis::OpId>>;
void Require(bool condition, Error error) { if (!condition) throw error; }
class LiteralBudget {
 public:
  explicit LiteralBudget(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit); remaining_.bytes -= count * width;
  }
  template <class T> void Size(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  analysis::SemanticBudget initial_, remaining_;
};
// This conditional proof is discharged by the family receiver projection, not
// inferred from a Language operand here. Index it once for both use and interval
// censuses; an uninitialized/repeated-super check never becomes a literal elision.
std::vector<std::uint8_t> InitializedReceiverChecks(const analysis::ValueProgram& graph,
    std::span<const analysis::OpId> guards, LiteralBudget& budget, const EvaluationPlan* evaluation) {
  std::vector<std::uint8_t> initialized;
  if (guards.empty()) return initialized;
  Require(!evaluation && graph.phases.empty(), Error::kReceiver);
  budget.Size(initialized, graph.ops.size());
  for (std::size_t i = 0; i < guards.size(); ++i) {
    budget.Step(); const auto id = guards[i];
    Require((i == 0 || guards[i - 1] < id) && id.value < graph.ops.size(), Error::kReceiver);
    const auto& op = graph.ops[id.value];
    const auto* check = std::get_if<analysis::ValueSuperCheck>(&op.operation);
    Require(check && op.life == analysis::ValueLife::kLive && !op.result && op.use_count == 1 &&
        op.first_use.value < graph.uses.size(), Error::kReceiver);
    const auto& use = graph.uses[op.first_use.value];
    Require(use.life == analysis::ValueLife::kLive && use.owner == decltype(use.owner){id} &&
        use.role == analysis::OperandRole::kGuardValue && use.purpose == analysis::ReadPurpose::kProtocol,
        Error::kReceiver);
    switch (check->kind) {
      case analysis::SuperCheckKind::kRequireInitialized: initialized[id.value] = 1; break;
      case analysis::SuperCheckKind::kRequireUninitialized: break;
    }
  }
  return initialized;
}
analysis::DominatorTree Flow(const analysis::ValueProgram& graph, LiteralBudget& budget, const EvaluationPlan* evaluation, const CellEvaluation* cells) {
  if (evaluation) {
    Require(evaluation->flow == EvaluationFlow::kPhased && !graph.phases.empty() &&
        evaluation->operations.size() == graph.ops.size(), Error::kFlow);
    auto proof = analysis::VerifyPhasedValueFlow(graph, budget.Remaining());
    if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&proof)) {
      if (*error == analysis::ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == analysis::ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<analysis::PhasedValueFlowProof>(proof).work);
    auto& dominators = std::get<analysis::PhasedValueFlowProof>(proof).dominators;
    budget.Admit(dominators.nodes.capacity(), sizeof(analysis::DominatorNode));
    // Guard omission requires a fresh completed-store/owner witness, not merely
    // a caller-supplied placement. Keep catch edges and original PC identities.
    if (evaluation->cells) {
      const auto cells = VerifyPhasedCellEvaluation(graph, *evaluation->cells, budget.Remaining());
      if (const auto* error = std::get_if<CellEvaluationError>(&cells)) {
        if (*error == CellEvaluationError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == CellEvaluationError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kFlow;
      }
      budget.Step(std::get<std::uint64_t>(cells));
    }
    const auto aliases = VerifyEvaluationAliases(graph, *evaluation, budget.Remaining());
    if (const auto* error = std::get_if<EvaluationFailure>(&aliases)) {
      if (*error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<std::uint64_t>(aliases));
    const auto elided = VerifyElidedPureEvaluations(graph, *evaluation, budget.Remaining());
    if (const auto* error = std::get_if<EvaluationFailure>(&elided)) {
      if (*error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<std::uint64_t>(elided));
    return std::move(dominators);
  }
  auto proof = analysis::VerifyValueFlow(graph, budget.Remaining());
  if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&proof)) {
    if (*error == analysis::ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kFlow;
  }
  budget.Step(std::get<analysis::ValueFlowProof>(proof).work);
  auto& dominators = std::get<analysis::ValueFlowProof>(proof).dominators;
  budget.Admit(dominators.nodes.capacity(), sizeof(analysis::DominatorNode));
  if (cells) {
    const auto checked = VerifyCellEvaluation(graph, *cells, budget.Remaining());
    if (const auto* error = std::get_if<CellEvaluationError>(&checked)) {
      if (*error == CellEvaluationError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == CellEvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<std::uint64_t>(checked));
  }
  return std::move(dominators);
}
struct LiteralPosition { std::optional<analysis::WorkBlockId> head; std::size_t order = 0; };
class LiteralPath {
 public:
  LiteralPath(const analysis::ValueProgram& graph, const EvaluationPlan* evaluation, LiteralBudget& budget,
      const analysis::DominatorTree& dominators, const CellEvaluation* cells)
      : graph_(graph), evaluation_(evaluation),
        cells_(evaluation ? (evaluation->cells ? &*evaluation->cells : nullptr) : cells),
        dominators_(dominators) {
    if (!evaluation) {
      budget.Size(normal_aliases_.operations, graph.ops.size());
      const auto aliases = BuildEvaluationAliases(graph, normal_aliases_, budget.Remaining());
      if (const auto* error = std::get_if<EvaluationFailure>(&aliases)) {
        if (*error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kFlow;
      }
      const auto cost = std::get<EvaluationAliasCost>(aliases);
      budget.Admit(cost.bytes, 1); budget.Step(cost.work);
    }
    auto effects = BuildEffectOrder(graph, budget.Remaining());
    if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&effects)) {
      if (*error == analysis::ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == analysis::ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    effects_ = std::get<EffectOrder>(std::move(effects));
    budget.Admit(effects_.allocated_bytes, 1); budget.Step(effects_.work);
    // Native initialization with already available RHSs is an all-path
    // effect chain. Forward must-reach and exact predecessor identities prove
    // each write executes once per allocation, including arbitrary pure CFG
    // splits/joins. Counts only bound this walk; they do not prove identity.
    std::vector<std::optional<analysis::OpId>> owners;
    std::vector<std::uint32_t> count;
    budget.Size(owners, graph.ops.size()); budget.Size(count, graph.ops.size());
    budget.Size(flat_, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const auto& op = graph.ops[i];
      if (op.life != analysis::ValueLife::kLive || op.use_count != 2 ||
          !(std::holds_alternative<analysis::ValueOwnWrite>(op.operation) ||
            std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) continue;
      const auto base = Resolve(graph.uses[op.first_use.value].operand);
      const auto* value = std::get_if<analysis::ValueId>(&base);
      const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
      if (!result || !AggregateAllocation(graph.ops[result->op.value].operation)) continue;
      owners[i] = result->op; ++count[result->op.value];
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); if (!count[i]) continue;
      const auto& allocation = graph.ops[i];
      analysis::OpId previous{static_cast<std::uint32_t>(i)};
      bool native = true;
      for (std::uint32_t n = 0; n < count[i]; ++n) {
        budget.Step(); const auto next = effects_.after[previous.value];
        if (!next || owners[next->value] != analysis::OpId{static_cast<std::uint32_t>(i)} ||
            effects_.before[next->value] != EffectPredecessor{previous}) { native = false; break; }
        const auto& write = graph.ops[next->value];
        const auto rhs = Resolve(graph.uses[write.first_use.value+1].operand);
        if (const auto* value = std::get_if<analysis::ValueId>(&rhs))
          if (!graph.values[value->value].domain.IsLanguage() || !Available(*value, allocation)) {
            native = false; break;
          }
        previous = *next;
      }
      flat_[i] = native;
    }
    if (!evaluation) return;
    std::vector<std::size_t> incoming;
    std::vector<std::optional<analysis::WorkBlockId>> next;
    std::vector<std::uint8_t> previous;
    budget.Size(incoming, graph.blocks.size()); budget.Size(next, graph.blocks.size());
    budget.Size(previous, graph.blocks.size()); budget.Size(positions_, graph.blocks.size());
    for (const auto& edge : graph.edges) { budget.Step(); ++incoming[edge.target.value]; }
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      budget.Step(); const auto& block = graph.blocks[b];
      std::optional<analysis::EdgeId> normal;
      std::size_t count = 0;
      for (std::size_t e = block.first_edge.value; e < std::size_t(block.first_edge.value) + block.edge_count; ++e) {
        budget.Step(); if (graph.edges[e].kind == analysis::FlowEdgeKind::kException) continue;
        ++count; normal = analysis::EdgeId{static_cast<std::uint32_t>(e)};
      }
      if (count != 1 || graph.phases[b].phase == analysis::ExceptionPhase::kCatch) continue;
      const auto& edge = graph.edges[normal->value];
      if (edge.kind != analysis::FlowEdgeKind::kFallthrough || incoming[edge.target.value] != 1 ||
          graph.phases[edge.target.value].phase == analysis::ExceptionPhase::kCatch) continue;
      next[b] = edge.target; previous[edge.target.value] = 1;
    }
    for (std::size_t root = 0; root < graph.blocks.size(); ++root) {
      budget.Step(); if (previous[root]) continue;
      std::size_t b = root, order = 0;
      for (;;) {
        budget.Step(); Require(!positions_[b].head, Error::kFlow);
        positions_[b] = {analysis::WorkBlockId{static_cast<std::uint32_t>(root)}, order++};
        if (!next[b]) break;
        b = next[b]->value;
      }
    }
  }
  analysis::ValueOperand Resolve(const analysis::ValueOperand& operand) const {
    return ResolveEvaluationOperand(evaluation_ ? *evaluation_ : normal_aliases_, operand);
  }
  bool Available(analysis::ValueId value, const analysis::ValueOp& site) const {
    return analysis::ValueAvailableAt(graph_, dominators_, value, site.block, site.order);
  }
  bool Same(analysis::WorkBlockId a, analysis::WorkBlockId b) const {
    if (!evaluation_) return a == b;
    return positions_[a.value].head && positions_[a.value].head == positions_[b.value].head;
  }
  bool Before(const analysis::ValueOp& a, const analysis::ValueOp& b) const {
    return effects_.Before(graph_, Id(a), b.block, b.order);
  }
  bool Alias(analysis::OpId id) const {
    return (evaluation_ ? *evaluation_ : normal_aliases_).operations[id.value].kind == EvaluationKind::kAlias;
  }
  bool IdentityPhi(const analysis::OperandUse& use) const {
    return IsIdentityPhiInput(evaluation_ ? *evaluation_ : normal_aliases_, use);
  }
  std::uint32_t Position(analysis::OpId id) const { return effects_.position[id.value]; }
  bool NativeBefore(analysis::OpId allocation, const analysis::ValueOp& write) const {
    if (flat_[allocation.value]) return Before(graph_.ops[allocation.value], write);
    if (evaluation_) {
      const auto& origin = graph_.ops[allocation.value];
      return Same(origin.block, write.block) &&
          (origin.block == write.block ? origin.order < write.order :
           positions_[origin.block.value].order < positions_[write.block.value].order);
    }
    // Late owned expression trees retain the existing local native interval.
    // This is a representation gap; never add a layout condition after the
    // common available-RHS/all-path proof has discharged initialization.
    return graph_.ops[allocation.value].block == write.block && Before(graph_.ops[allocation.value], write);
  }
  bool Elided(analysis::OpId id) const {
    return evaluation_ && evaluation_->operations[id.value].kind == EvaluationKind::kElidedPure;
  }
  bool InitializedGuard(analysis::OpId id) const {
    return cells_ && IsInitializedCellGuard(graph_, *cells_, id);
  }
  bool Phased() const { return evaluation_ != nullptr; }
  bool After(const analysis::ValueOp& last, const analysis::OperandUse& use) const {
    if (const auto* id = std::get_if<analysis::OpId>(&use.owner)) return Before(last, graph_.ops[id->value]);
    if (!use.incoming) return false;
    const auto* edge = std::get_if<analysis::EdgeId>(&*use.incoming);
    if (!edge) return false;
    const auto& flow = graph_.edges[edge->value];
    // No catch snapshot of a partially initialized object. Even if the only
    // remaining throw is resource exhaustion, keep this boundary conservative.
    return flow.kind != analysis::FlowEdgeKind::kException &&
        effects_.Before(graph_, Id(last), flow.source, graph_.blocks[flow.source.value].op_count);
  }
 private:
  analysis::OpId Id(const analysis::ValueOp& op) const {
    return {graph_.blocks[op.block.value].first_op.value + op.order};
  }
  static bool AggregateAllocation(const analysis::ValueOperation& operation) {
    return std::holds_alternative<analysis::ValueObjectAllocation>(operation) ||
        std::holds_alternative<analysis::ValueArrayAllocation>(operation);
  }
  const analysis::ValueProgram& graph_;
  const EvaluationPlan* evaluation_;
  const CellEvaluation* cells_;
  const analysis::DominatorTree& dominators_;
  std::vector<LiteralPosition> positions_;
  EvaluationPlan normal_aliases_;
  EffectOrder effects_;
  std::vector<std::uint8_t> flat_;
};
analysis::OpId Allocation(const analysis::ValueProgram& graph, analysis::OpId id, const LiteralPath& path, bool same_path, EvaluationInputSite* failure) {
  EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralAllocation, id});
  Require(id.value < graph.ops.size(), Error::kDefinition);
  const auto& op = graph.ops[id.value];
  const auto* write = std::get_if<analysis::ValueOwnWrite>(&op.operation);
  const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&op.operation);
  Require(op.life == analysis::ValueLife::kLive && (write || indexed) && !op.result && op.use_count == 2,
      Error::kDefinition);
  if (indexed) Require(indexed->kind == analysis::IndexedDefinition::kOwnStore ||
      indexed->kind == analysis::IndexedDefinition::kDefineField, Error::kDefinition);
  Require(indexed || write->kind == analysis::OwnWriteKind::kStore ||
      write->kind == analysis::OwnWriteKind::kDefineField, Error::kDefinition);
  const auto& base = graph.uses[op.first_use.value];
  const auto& stored = graph.uses[op.first_use.value + 1];
  Require(base.role == analysis::OperandRole::kBase && stored.role == analysis::OperandRole::kStoredValue &&
      base.purpose == analysis::ReadPurpose::kOperand && stored.purpose == analysis::ReadPurpose::kOperand,
      Error::kDefinition);
  const auto operand = path.Resolve(base.operand);
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  Require(value && value->value < graph.values.size(), Error::kReceiver);
  const auto& definition = graph.values[value->value];
  Require(definition.domain.IsLanguage(), Error::kDomain);
  const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
  Require(result && result->op.value < graph.ops.size(), Error::kReceiver);
  const auto& allocation = graph.ops[result->op.value];
  Require(allocation.life == analysis::ValueLife::kLive &&
      (indexed && indexed->kind == analysis::IndexedDefinition::kOwnStore ?
                 std::holds_alternative<analysis::ValueArrayAllocation>(allocation.operation) :
                 std::holds_alternative<analysis::ValueObjectAllocation>(allocation.operation)) &&
      allocation.result == *value && allocation.use_count == 0 &&
      (!same_path || path.Before(allocation, op)), Error::kReceiver);
  return result->op;
}
void EarlyValue(const analysis::ValueProgram& graph, const analysis::ValueOp& allocation,
    const analysis::ValueOperand& original, const LiteralPath& path, analysis::OpId owner, analysis::UseId use, EvaluationInputSite* failure) {
  EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralRhsAvailability, owner, use, std::get_if<analysis::ValueId>(&original) ? std::optional{std::get<analysis::ValueId>(original)} : std::nullopt});
  const auto operand = path.Resolve(original);
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  if (!value) return;
  const auto& definition = graph.values[value->value];
  Require(definition.domain.IsLanguage(), Error::kDomain);
  // The fresh common SSA proof owns availability at the actual allocation,
  // including successful Commit and dominating phi/catch entry. A native
  // initializer's all-path effect chain or local/phase interval cannot reenter
  // a dominating phi block and overwrite its source edge-copy storage.
  // Do not replace this proof with producer/block identity matching.
  Require(path.Available(*value, allocation), Error::kLateValue);
}
bool Aggregate(const analysis::ValueOperation& op) {
  return std::holds_alternative<analysis::ValueArrayAllocation>(op) ||
      std::holds_alternative<analysis::ValueObjectAllocation>(op);
}
bool LiteralExpression(const analysis::ValueProgram& graph, const analysis::ValueOp& op, std::size_t index, Guards guards) {
  const auto shape = DescribeEvaluation(op.operation);
  if (!shape || !shape->expression || shape->Count() != op.use_count ||
      shape->inputs != InputEvaluation::kEager || !op.result ||
      !graph.values[op.result->value].domain.IsLanguage()) return false;
  switch (shape->source) {
    case SourceEvaluation::kPure:
      if (!analysis::CanErasePureOp(op)) return false;
      break;
    case SourceEvaluation::kDirect: case SourceEvaluation::kSnapshotInputs: break;
    case SourceEvaluation::kSuspending: case SourceEvaluation::kInvocation:
    case SourceEvaluation::kUnsupported: return false;
  }
  // Expression capability never discharges module TDZ, single ownership,
  // capture environment, escape, effect/catch order or repeated-input reads.
  // The common module proof owns this paired guard; interval/use proofs below
  // own all other obligations, regardless of operation kind or block layout.
  return !std::holds_alternative<analysis::ValueModuleRead>(op.operation) ||
      (index < guards.size() && guards[index]);
}
// A native member call consumes its base through the callee Reference. The
// duplicate wire receiver is not a second evaluation. Recognize only the exact
// SSA base/property pair; CallPlan independently checks the source binding.
bool ReferenceReceiver(const analysis::ValueProgram& graph, analysis::UseId use, const LiteralPath& path) {
  const auto& occurrence = graph.uses[use.value];
  const auto* owner = std::get_if<analysis::OpId>(&occurrence.owner);
  if (!owner || owner->value >= graph.ops.size()) return false;
  const auto& call = graph.ops[owner->value];
  const auto* invoke = std::get_if<analysis::ValueInvoke>(&call.operation);
  if (!invoke || invoke->receiver != analysis::InvokeReceiver::kExplicit ||
      call.use_count < 2 || use.value != call.first_use.value + 1) return false;
  const auto callee_operand = path.Resolve(graph.uses[call.first_use.value].operand);
  const auto* callee = std::get_if<analysis::ValueId>(&callee_operand);
  if (!callee) return false;
  const auto* result = std::get_if<analysis::OpResult>(&graph.values[callee->value].origin);
  if (!result) return false;
  const auto& property = graph.ops[result->op.value];
  if (!(std::holds_alternative<analysis::ValueNamedRead>(property.operation) ||
        std::holds_alternative<analysis::ValuePropertyRead>(property.operation)) || !property.use_count) return false;
  const auto base_operand = path.Resolve(graph.uses[property.first_use.value].operand);
  const auto receiver_operand = path.Resolve(occurrence.operand);
  const auto* base = std::get_if<analysis::ValueId>(&base_operand);
  const auto* receiver = std::get_if<analysis::ValueId>(&receiver_operand);
  return base && receiver && *base == *receiver && path.Before(property, call);
}
// Check laminar allocation/initialization intervals with one stack. Nested
// creation may execute in native literal syntax only when every intervening
// observable operation is an owned creation, definition, or single-use read
// expression. Subtrees stay within their property interval and preserve
// operand order. Name globals retain their actual read site and lexical
// destination; var/object-global lookups, catch snapshots and escapes refuse.
void VerifyNesting(const analysis::ValueProgram& graph, const LiteralPlan& plan,
    const std::vector<std::optional<analysis::OpId>>& owners, LiteralBudget& budget, Guards guards,
    const LiteralPath& path, const EvaluationPlan* evaluation,
    std::span<const std::uint8_t> initialized_receivers, EvaluationInputSite* failure) {
  if (plan.nested.empty()) return;
  Require(plan.nested.size() == graph.ops.size(), Error::kFlow);
  std::vector<std::optional<analysis::OpId>> ends;
  std::vector<analysis::OpId> stack;
  std::vector<std::size_t> first_read, last_write;
  budget.Size(first_read, graph.ops.size()); budget.Size(last_write, graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) { budget.Step(); first_read[i] = last_write[i] = i; }
  std::vector<std::optional<analysis::OpId>> value_owners;
  budget.Size(value_owners, graph.ops.size());
  budget.Size(ends, graph.ops.size()); budget.Size(stack, graph.ops.size());
  PhaseBudget phase_budget(budget.Remaining());
  std::optional<PhaseCorridors> phases;
  // This pass proves literal intervals; it needs catch indexing, not a second
  // interval index derived from a candidate EvaluationPlan::literals witness.
  if (evaluation) phases.emplace(graph, *evaluation, phase_budget, PhaseLiteralIntervals::kExclude);
  for (const auto& object : plan.objects) {
    budget.Step(); if (object.form == LiteralForm::kStatements) continue;
    ends[object.allocation.value] = plan.definitions[object.first + object.count - 1];
  }
  for (std::size_t i = graph.ops.size(); i-- > 0;) {
    budget.Step(); if (!plan.nested[i]) continue;
    EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralNesting, analysis::OpId{static_cast<std::uint32_t>(i)}, plan.nested[i]});
    Require(plan.nested[i]->value < graph.uses.size(), Error::kDefinition);
    const auto* parent = std::get_if<analysis::OpId>(&graph.uses[plan.nested[i]->value].owner);
    Require(parent && parent->value > i && parent->value < graph.ops.size() &&
        path.Before(graph.ops[i], graph.ops[parent->value]), Error::kDefinition);
    value_owners[i] = owners[parent->value] ? owners[parent->value] : value_owners[parent->value];
    Require(value_owners[i] && value_owners[i]->value < i, Error::kDefinition);
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!plan.nested[i]) continue;
    EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralNesting, analysis::OpId{static_cast<std::uint32_t>(i)}, plan.nested[i]});
    const auto& op = graph.ops[i]; const auto use = *plan.nested[i];
    Require(op.life == analysis::ValueLife::kLive && (Aggregate(op.operation) || LiteralExpression(graph, op, i, guards)) && op.result &&
        use.value < graph.uses.size(), Error::kDefinition);
    const auto& occurrence = graph.uses[use.value];
    const auto* parent = std::get_if<analysis::OpId>(&occurrence.owner);
    Require(parent && value_owners[i] && (owners[parent->value] || plan.nested[parent->value]) &&
        path.Resolve(occurrence.operand).index() == analysis::ValueOperand{*op.result}.index() &&
        std::get<analysis::ValueId>(path.Resolve(occurrence.operand)) == *op.result &&
        value_owners[i]->value < i && parent->value > i, Error::kDefinition);
  }
  // Aliases keep original use chains. One canonical census also sees every
  // indirect consumer and catch/phi observation, without walking aliases per
  // nested node or silently treating transport reads as unique ownership.
  for (std::size_t u = 0; u < graph.uses.size(); ++u) {
    budget.Step(); const auto& read = graph.uses[u];
    if (read.life != analysis::ValueLife::kLive) continue;
    const auto* user = std::get_if<analysis::OpId>(&read.owner);
    // Flow independently certified dead closure before this census. Its
    // original operands/PCs remain in the graph and later source anchors.
    if (user && (graph.ops[user->value].life != analysis::ValueLife::kLive || path.Alias(*user) || path.Elided(*user) || path.InitializedGuard(*user) ||
        (!initialized_receivers.empty() && initialized_receivers[user->value]))) continue;
    const auto operand = path.Resolve(read.operand);
    const auto* value = std::get_if<analysis::ValueId>(&operand);
    const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
    if (!result || !plan.nested[result->op.value]) continue;
    const auto i = result->op.value; const analysis::UseId at{static_cast<std::uint32_t>(u)};
    Require(at == *plan.nested[i] ||
        (user && plan.nested[user->value] && ReferenceReceiver(graph, at, path)) ||
        (user && i < guards.size() && guards[i] == *user) ||
        (user && owners[user->value] == result->op && at == graph.ops[user->value].first_use), Error::kObserved);
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!plan.nested[i] || Aggregate(graph.ops[i].operation)) continue;
    const auto& op = graph.ops[i];
    const auto shape = DescribeEvaluation(op.operation);
    Require(shape.has_value(), Error::kDefinition);
    std::optional<std::size_t> previous;
    for (std::uint32_t u = 0; u < op.use_count; ++u) {
      budget.Step(); const analysis::UseId use{op.first_use.value + u};
      Require(!CheckEvaluationUse(graph, graph.uses[use.value], *shape, u), Error::kDefinition);
      if (ReferenceReceiver(graph, use, path)) continue;
      const auto operand = path.Resolve(graph.uses[use.value].operand);
      const auto* value = std::get_if<analysis::ValueId>(&operand);
      const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
      if (!result) continue;
      if (plan.nested[result->op.value] == use) {
        Require(!PredicateSnapshot(graph, op, operand), Error::kLateValue);
        // An aggregate operand occupies its entire initialization interval,
        // not just its allocation site. Arguments must be complete, disjoint
        // and in source operand order before their consuming expression.
        const auto finish = ends[result->op.value] ? ends[result->op.value]->value : result->op.value;
        Require(finish < i &&
            value_owners[result->op.value] == value_owners[i] &&
            (!previous || *previous < first_read[result->op.value]), Error::kDefinition);
        first_read[i] = std::min(first_read[i], first_read[result->op.value]);
        previous = finish;
      } else Require(result->op.value < value_owners[i]->value, Error::kLateValue);
    }
  }
  std::size_t depth = 0;
  std::optional<analysis::OpId> catch_site;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto& op = graph.ops[i];
    if (depth) Require(path.Same(graph.ops[stack[0].value].block, op.block), Error::kFlow);
    if (op.life != analysis::ValueLife::kLive || path.Alias({static_cast<std::uint32_t>(i)}) || path.InitializedGuard({static_cast<std::uint32_t>(i)})) continue;
    // Every effect folded into native aggregate syntax retains the outer
    // allocation's dispatch and staged catch values, not merely a normal path.
    // Comparing each site's copy bundle once is bounded by original copy volume.
    // Nonthrowing allocations still retain ordered creation/identity, but
    // cannot change a catch snapshot and have no exceptional exit to compare.
    // A nested module expression owns both throwing events. The peer index
    // proves the TDZ pair, but its guard must retain the allocation's catch
    // snapshot too; checking only the read would leave the second event free.
    const bool nested_guard = i < guards.size() && guards[i] &&
        std::holds_alternative<analysis::ValueGuard>(op.operation) && plan.nested[guards[i]->value];
    if (depth && phases && (owners[i] || plan.nested[i] || nested_guard) && OrderedEvaluation(op) &&
        op.exceptional == analysis::ExceptionalCompletion::kMayThrow) {
      const analysis::OpId site{static_cast<std::uint32_t>(i)};
      // A nonthrowing allocation has no catch signature. Its first real throw
      // anchors the initializer's signature; original lexical try ownership is
      // independently checked by TreeFlow against actual expression scopes.
      // Until staging can precede this allocation, every folded throw must be
      // unstaged. Never execute a native initializer before its catch copies.
      if (!catch_site) {
        Require(graph.ops[stack[0].value].exceptional == analysis::ExceptionalCompletion::kImpossible &&
            phases->SameCatch(site, site) && !phases->HasCatchCopies(site), Error::kObserved);
        catch_site = site;
      } else Require(phases->SameCatch(site, *catch_site), Error::kObserved);
    }
    if (Aggregate(op.operation) && (ends[i] || plan.nested[i])) {
      if (depth) {
        Require(plan.nested[i].has_value(), Error::kObserved);
        Require(value_owners[i] == stack[depth - 1], Error::kDefinition);
      } else {
        Require(!plan.nested[i], Error::kDefinition);
        catch_site = op.exceptional == analysis::ExceptionalCompletion::kMayThrow ?
            std::optional{analysis::OpId{static_cast<std::uint32_t>(i)}} : std::nullopt;
      }
      if (ends[i]) stack[depth++] = {static_cast<std::uint32_t>(i)};
    } else if (depth) {
      if (owners[i]) {
        Require(owners[i] == stack[depth - 1], Error::kDefinition);
        const analysis::UseId rhs{op.first_use.value + 1};
        const auto operand = path.Resolve(graph.uses[rhs.value].operand);
        const auto* value = std::get_if<analysis::ValueId>(&operand);
        const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
        if (result && plan.nested[result->op.value] != rhs)
          Require(result->op.value < stack[0].value, Error::kLateValue);
        if (result && plan.nested[result->op.value] == rhs &&
            !Aggregate(graph.ops[result->op.value].operation))
          Require(first_read[result->op.value] > last_write[stack[depth - 1].value], Error::kDefinition);
        last_write[stack[depth - 1].value] = i;
      } else if (plan.nested[i]) {
        Require(value_owners[i] == stack[depth - 1], Error::kObserved);
        for (std::uint32_t p = 0; p < op.use_count; ++p) {
          budget.Step(); const analysis::UseId use{op.first_use.value + p};
          if (ReferenceReceiver(graph, use, path)) continue;
          const auto operand = path.Resolve(graph.uses[use.value].operand);
          const auto* value = std::get_if<analysis::ValueId>(&operand);
          const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
          if (result && plan.nested[result->op.value] != use)
            Require(result->op.value < stack[0].value, Error::kLateValue);
        }
      } else if (nested_guard) {
        Require(value_owners[guards[i]->value] == stack[depth - 1], Error::kObserved);
      } else Require((!initialized_receivers.empty() && initialized_receivers[i]) ||
          analysis::CanErasePureOp(op), Error::kObserved);
    }
    if (depth && ends[stack[depth - 1].value] == analysis::OpId{static_cast<std::uint32_t>(i)}) --depth;
  }
  Require(depth == 0, Error::kDefinition);
  budget.Step(phase_budget.Cost().work);
}
// Staged buffer writes remain at their original CFG sites. Until the last
// write, only these own-field base uses may observe the fresh object. Acyclic
// control plus dominance excludes an escape followed by a later initialization.
void VerifyStaged(const analysis::ValueProgram& graph, const LiteralPlan& plan,
    std::span<const std::optional<analysis::OpId>> owners, LiteralBudget& budget, const LiteralPath& path, EvaluationInputSite* failure) {
  if (plan.staged.empty()) return;
  analysis::NormalFlow flow; budget.Size(flow.blocks, graph.blocks.size());
  std::vector<std::size_t> incoming, order, rank;
  budget.Size(incoming, graph.blocks.size()); budget.Size(order, graph.blocks.size()); budget.Size(rank, graph.blocks.size());
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); auto& output = flow.blocks[b]; const auto& block = graph.blocks[b];
    // Phased Before/Commit/Catch edges all participate in dominance. Never
    // drop an exception edge to make a private initialization appear complete.
    Require(block.edge_count <= output.successors.size(), Error::kFlow);
    output.successor_count = block.edge_count;
    for (std::uint32_t e = 0; e < block.edge_count; ++e) {
      budget.Step(); const auto& edge = graph.edges[block.first_edge.value + e];
      output.successors[e] = {{edge.target.value}, edge.kind}; ++incoming[edge.target.value];
    }
  }
  std::size_t count = 0;
  for (std::size_t b = 0; b < incoming.size(); ++b) { budget.Step(); if (!incoming[b]) order[count++] = b; }
  for (std::size_t i = 0; i < count; ++i) {
    budget.Step(); const auto b = order[i]; rank[b] = i;
    for (std::uint32_t e = 0; e < graph.blocks[b].edge_count; ++e) {
      budget.Step(); const auto target = graph.edges[graph.blocks[b].first_edge.value + e].target.value;
      if (!--incoming[target]) order[count++] = target;
    }
  }
  {
    // This is a whole-CFG acyclicity obligation, not a failing instruction.
    // Do not borrow a PC from a previously visited allocation or branch.
    EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralStagedAcyclic});
    Require(count == graph.blocks.size(), Error::kFlow);
  }
  const auto allowance = budget.Remaining();
  auto built = analysis::BuildNormalDominators(flow, {}, {allowance.bytes, allowance.work});
  if (const auto* error = std::get_if<analysis::DominatorError>(&built)) {
    if (*error == analysis::DominatorError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::DominatorError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kFlow;
  }
  const auto& dom = std::get<analysis::DominatorTree>(built);
  budget.Step(dom.work.steps); budget.Admit(dom.work.allocated_bytes, 1);
  std::vector<std::optional<analysis::OpId>> finals, allocations;
  budget.Size(finals, graph.values.size()); budget.Size(allocations, graph.values.size());
  for (const auto& object : plan.objects) {
    budget.Step(); if (object.form != LiteralForm::kStatements) continue;
    EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralStagedStorage, object.allocation});
    const auto& allocation = graph.ops[object.allocation.value];
    const auto* buffer = std::get_if<analysis::ValueObjectAllocation>(&allocation.operation);
    Require(buffer && buffer->literal, Error::kDefinition);
    auto last = object.allocation;
    for (std::uint32_t i = 0; i < object.count; ++i) {
      budget.Step(); const auto id = plan.definitions[object.first + i]; const auto& op = graph.ops[id.value];
      Require(std::holds_alternative<analysis::ValueOwnWrite>(op.operation), Error::kDefinition);
      if (rank[op.block.value] > rank[graph.ops[last.value].block.value] ||
          (op.block == graph.ops[last.value].block && op.order > graph.ops[last.value].order)) last = id;
    }
    finals[allocation.result->value] = last;
    allocations[allocation.result->value] = object.allocation;
  }
  // Resolve each occurrence once. Copy aliases in phased IR must not hide an
  // early escape; conversely, a pure register copy is not an observation.
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    budget.Step(); const auto& use = graph.uses[i];
    if (use.life != analysis::ValueLife::kLive) continue;
    const auto operand = path.Resolve(use.operand);
    const auto* value = std::get_if<analysis::ValueId>(&operand);
    if (!value || !finals[value->value]) continue;
    const auto& final = graph.ops[finals[value->value]->value];
    analysis::WorkBlockId block; std::size_t position;
    if (const auto* user = std::get_if<analysis::OpId>(&use.owner)) {
      const auto& op = graph.ops[user->value];
      if (op.life != analysis::ValueLife::kLive || path.Alias(*user) || path.Elided(*user)) continue;
      if (owners[user->value] == allocations[value->value] && i == op.first_use.value) continue;
      block = op.block; position = op.order;
    } else {
      Require(use.incoming && std::holds_alternative<analysis::EdgeId>(*use.incoming), Error::kObserved);
      const auto& edge = graph.edges[std::get<analysis::EdgeId>(*use.incoming).value];
      // A before-throw snapshot does not prove completion of the final write.
      Require(edge.kind != analysis::FlowEdgeKind::kException, Error::kObserved);
      block = edge.source; position = graph.blocks[block.value].op_count;
    }
    auto completed = final.block;
    if (path.Phased()) {
      const auto& before = graph.blocks[final.block.value];
      std::optional<analysis::WorkBlockId> commit;
      for (std::uint32_t e = 0; e < before.edge_count; ++e) {
        budget.Step(); const auto& edge = graph.edges[before.first_edge.value + e];
        if (edge.kind == analysis::FlowEdgeKind::kException) continue;
        const auto& phase = graph.phases[edge.target.value];
        Require(!commit && phase.phase == analysis::ExceptionPhase::kCommit &&
            phase.operation == finals[value->value], Error::kFlow);
        commit = edge.target;
      }
      Require(commit.has_value(), Error::kFlow); completed = *commit;
    }
    Require(dom.Dominates({completed.value}, {block.value}) &&
        (final.block != block || final.order < position), Error::kObserved);
  }
}
struct FieldOwner { analysis::OpId allocation; analysis::OpId definition; };
std::variant<std::uint64_t, LiteralError> VerifyLiterals(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, const EvaluationPlan* evaluation,
    analysis::SemanticBudget allowance, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards, const CellEvaluation* cells, EvaluationInputSite* failure) {
  try {
    LiteralBudget budget(allowance);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    const auto dominators = Flow(graph, budget, evaluation, cells);
    Require(guards.empty() || guards.size() == graph.ops.size(), Error::kDefinition);
    Require(projected.empty() || projected.size() == graph.ops.size(), Error::kDefinition);
    for (std::size_t i = 0; i < guards.size(); ++i) {
      budget.Step();
      if (guards[i]) Require(guards[i]->value < graph.ops.size() &&
          guards[guards[i]->value] == analysis::OpId{static_cast<std::uint32_t>(i)}, Error::kDefinition);
    }
    LiteralPath path(graph, evaluation, budget, dominators, cells);
    Require(plan.objects.size() <= graph.ops.size() && plan.definitions.size() <= graph.ops.size(), Error::kCoverage);
    Require(plan.nested.empty() || plan.nested.size() == graph.ops.size(), Error::kCoverage);
    Require(plan.staged.empty() || plan.staged.size() == graph.ops.size(), Error::kCoverage);
    std::vector<std::optional<analysis::OpId>> owners;
    std::vector<std::uint8_t> allocations;
    budget.Admit(1, sizeof(owners) + sizeof(allocations));
    budget.Size(owners, graph.ops.size()); budget.Size(allocations, graph.ops.size());
    std::size_t next = 0;
    for (const auto& object : plan.objects) {
      budget.Step();
      Require(object.allocation.value < graph.ops.size() && !allocations[object.allocation.value] &&
          object.first == next && object.count != 0 && object.count <= plan.definitions.size() - next,
          Error::kCoverage);
      Require(object.form == LiteralForm::kExpression || object.form == LiteralForm::kStatements, Error::kDefinition);
      allocations[object.allocation.value] = 1;
      const auto& allocation = graph.ops[object.allocation.value];
      auto prior = object.allocation;
      std::optional<std::uint32_t> previous_index;
      for (std::uint32_t i = 0; i < object.count; ++i) {
        budget.Step();
        const auto id = plan.definitions[next++];
        Require(Allocation(graph, id, path, object.form == LiteralForm::kExpression, failure) == object.allocation && !owners[id.value], Error::kCoverage);
        Require(IsStagedLiteral(&plan, id) == (object.form == LiteralForm::kStatements), Error::kCoverage);
        const auto& op = graph.ops[id.value];
        if (const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&op.operation);
            indexed && indexed->kind == analysis::IndexedDefinition::kOwnStore) {
          const auto& array = std::get<analysis::ValueArrayAllocation>(allocation.operation);
          // Buffered primitives may be replaced, but RHS evaluations must stay
          // in increasing element order. Constants/AST validation checks gaps.
          Require(array.literal ? (!previous_index || indexed->index > *previous_index) :
              indexed->index == i, Error::kDefinition);
          previous_index = indexed->index;
        }
        if (object.form == LiteralForm::kStatements) {
          Require(plan.nested.empty() || !plan.nested[id.value], Error::kDefinition);
          owners[id.value] = object.allocation; continue;
        }
        EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralOrder, id});
        Require(path.Before(graph.ops[prior.value], op), Error::kDefinition); prior = id;
        const analysis::UseId rhs{op.first_use.value + 1};
        const auto operand = path.Resolve(graph.uses[rhs.value].operand);
        const auto* value = std::get_if<analysis::ValueId>(&operand);
        const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
        const bool nested = result && !plan.nested.empty() && plan.nested[result->op.value] == rhs;
        if (nested) Require(path.Before(allocation, graph.ops[result->op.value]), Error::kLateValue);
        else EarlyValue(graph, allocation, operand, path, id, rhs, failure);
        owners[id.value] = object.allocation;
      }
    }
    Require(next == plan.definitions.size(), Error::kCoverage);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const bool own = (projected.empty() || !projected[i]) && graph.ops[i].life == analysis::ValueLife::kLive && !IsReceiverField(graph, graph.ops[i]) &&
          (std::holds_alternative<analysis::ValueOwnWrite>(graph.ops[i].operation) || std::holds_alternative<analysis::ValueIndexedWrite>(graph.ops[i].operation));
      Require(owners[i].has_value() == own && (!IsStagedLiteral(&plan, {static_cast<std::uint32_t>(i)}) || own), Error::kCoverage);
    }
    VerifyStaged(graph, plan, owners, budget, path, failure);
    const auto initialized_receivers = InitializedReceiverChecks(graph, receiver_guards, budget, evaluation);
    VerifyNesting(graph, plan, owners, budget, guards, path, evaluation, initialized_receivers, failure);
    {
      std::vector<std::optional<std::size_t>> objects;
      budget.Size(objects, graph.values.size());
      for (std::size_t i = 0; i < plan.objects.size(); ++i) {
        budget.Step(); objects[graph.ops[plan.objects[i].allocation.value].result->value] = i;
      }
      // One census over canonical uses, not one graph/alias walk per field.
      for (std::size_t i = 0; i < graph.uses.size(); ++i) {
        budget.Step(); const auto& use = graph.uses[i];
        if (use.life != analysis::ValueLife::kLive) continue;
        const auto operand = path.Resolve(use.operand);
        const auto* value = std::get_if<analysis::ValueId>(&operand);
        if (!value || !objects[value->value]) continue;
        const auto& object = plan.objects[*objects[value->value]];
        if (object.form == LiteralForm::kStatements) continue;
        if (const auto* op = std::get_if<analysis::OpId>(&use.owner)) {
          if (graph.ops[op->value].life != analysis::ValueLife::kLive || path.Alias(*op) || path.Elided(*op)) continue;
          if (owners[op->value] == object.allocation && graph.ops[op->value].first_use.value == i) continue;
        }
        if (path.IdentityPhi(use)) continue;
        const auto last = plan.definitions[object.first + std::size_t(object.count) - 1];
        EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralUse, std::get_if<analysis::OpId>(&use.owner) ? std::optional{std::get<analysis::OpId>(use.owner)} : std::nullopt, analysis::UseId{static_cast<std::uint32_t>(i)}, value ? std::optional{*value} : std::nullopt});
        Require(path.After(graph.ops[last.value], use), Error::kObserved);
      }
      return budget.Work();
    }
  } catch (Error error) { return error;
  } catch (EvaluationError error) {
    return error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
        error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kFlow;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<LiteralPlan, LiteralError> BuildLiterals(
    const analysis::ValueProgram& graph, const EvaluationPlan* evaluation, analysis::SemanticBudget allowance, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards, const CellEvaluation* cells, EvaluationInputSite* failure) {
  try {
    LiteralBudget budget(allowance);
    const auto dominators = Flow(graph, budget, evaluation, cells);
    Require(guards.empty() || guards.size() == graph.ops.size(), Error::kDefinition);
    Require(projected.empty() || projected.size() == graph.ops.size(), Error::kDefinition);
    for (std::size_t i = 0; i < guards.size(); ++i) {
      budget.Step();
      if (guards[i]) Require(guards[i]->value < graph.ops.size() &&
          guards[guards[i]->value] == analysis::OpId{static_cast<std::uint32_t>(i)}, Error::kDefinition);
    }
    LiteralPath path(graph, evaluation, budget, dominators, cells);
    Require(graph.ops.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    std::size_t count = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step(); const auto& op = graph.ops[i];
      count += (projected.empty() || !projected[i]) && op.life == analysis::ValueLife::kLive && !IsReceiverField(graph, op) && (std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation));
    }
    std::vector<FieldOwner> fields;
    LiteralPlan plan{graph.revision, graph.cfg_revision};
    budget.Admit(1, sizeof(fields) + sizeof(plan)); budget.Size(fields, count);
    std::size_t next = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const auto& op = graph.ops[i];
      if ((!projected.empty() && projected[i]) || op.life != analysis::ValueLife::kLive || IsReceiverField(graph, op) || !(std::holds_alternative<analysis::ValueOwnWrite>(op.operation) || std::holds_alternative<analysis::ValueIndexedWrite>(op.operation))) continue;
      const analysis::OpId id{static_cast<std::uint32_t>(i)};
      fields[next++] = {Allocation(graph, id, path, false, failure), id};
    }
    std::sort(fields.begin(), fields.end(), [&](const auto& a, const auto& b) {
      budget.Step();
      return a.allocation != b.allocation ? a.allocation < b.allocation : path.Position(a.definition) < path.Position(b.definition);
    });
    std::size_t objects = 0;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); objects += i == 0 || fields[i].allocation != fields[i - 1].allocation;
    }
    budget.Size(plan.objects, objects); budget.Size(plan.definitions, count);
    next = 0;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step();
      if (i == 0 || fields[i].allocation != fields[i - 1].allocation)
        plan.objects[next++] = {fields[i].allocation, static_cast<std::uint32_t>(i), 0};
      ++plan.objects[next - 1].count;
      plan.definitions[i] = fields[i].definition;
    }
    for (auto& object : plan.objects) {
      for (std::uint32_t p = 0; p < object.count; ++p) {
        budget.Step(); const auto id = plan.definitions[object.first + p];
        if (!path.NativeBefore(object.allocation, graph.ops[id.value])) object.form = LiteralForm::kStatements;
      }
      if (object.form == LiteralForm::kStatements) {
        if (plan.staged.empty()) budget.Size(plan.staged, graph.ops.size());
        for (std::uint32_t p = 0; p < object.count; ++p) { budget.Step(); plan.staged[plan.definitions[object.first + p].value] = 1; }
      }
    }
    {
      for (const auto& object : plan.objects) for (std::uint32_t p = 0; p < object.count; ++p) {
        budget.Step(); if (object.form == LiteralForm::kStatements) continue;
        const auto& definition = graph.ops[plan.definitions[object.first + p].value];
        const analysis::UseId rhs{definition.first_use.value + 1};
        const auto operand = path.Resolve(graph.uses[rhs.value].operand);
        const auto* value = std::get_if<analysis::ValueId>(&operand);
        const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
        if (!result || !(Aggregate(graph.ops[result->op.value].operation) || LiteralExpression(graph, graph.ops[result->op.value], result->op.value, guards)) ||
            !path.Before(graph.ops[object.allocation.value], graph.ops[result->op.value])) continue;
        if (plan.nested.empty()) budget.Size(plan.nested, graph.ops.size());
        Require(!plan.nested[result->op.value], Error::kObserved);
        plan.nested[result->op.value] = rhs;
      }
    }
    if (!plan.nested.empty()) {
      // Consumers follow their inputs in SSA order; a reverse scan visits each
      // late read tree once. Shared values and aggregate receiver observations
      // are rejected by the independent use census below.
      std::vector<std::optional<analysis::OpId>> allocation;
      budget.Size(allocation, graph.ops.size());
      for (const auto& object : plan.objects) for (std::uint32_t p = 0; p < object.count; ++p) {
        budget.Step(); allocation[plan.definitions[object.first + p].value] = object.allocation;
      }
      for (std::size_t i = graph.ops.size(); i-- > 0;) {
        budget.Step(); if (!plan.nested[i]) continue;
    EvaluationInputScope site(failure, {EvaluationInputCheck::kLiteralNesting, analysis::OpId{static_cast<std::uint32_t>(i)}, plan.nested[i]});
        const auto parent = std::get<analysis::OpId>(graph.uses[plan.nested[i]->value].owner);
        Require(allocation[parent.value].has_value(), Error::kDefinition);
        allocation[i] = allocation[parent.value];
        if (Aggregate(graph.ops[i].operation)) continue;
        const auto& op = graph.ops[i];
        for (std::uint32_t p = 0; p < op.use_count; ++p) {
          budget.Step(); const analysis::UseId use{op.first_use.value + p};
          if (ReferenceReceiver(graph, use, path)) continue;
          const auto operand = path.Resolve(graph.uses[use.value].operand);
          const auto* value = std::get_if<analysis::ValueId>(&operand);
          const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
          if (!result || result->op.value < allocation[i]->value) continue;
          Require(!PredicateSnapshot(graph, op, operand), Error::kLateValue);
          Require(Aggregate(graph.ops[result->op.value].operation) ||
              LiteralExpression(graph, graph.ops[result->op.value], result->op.value, guards), Error::kLateValue);
          Require(!plan.nested[result->op.value], Error::kObserved);
          plan.nested[result->op.value] = use;
        }
      }
    }
    const auto verified = VerifyLiterals(graph, plan, evaluation, budget.Remaining(), guards, projected, receiver_guards, cells, failure);
    if (const auto* error = std::get_if<Error>(&verified)) return *error;
    budget.Step(std::get<std::uint64_t>(verified));
    plan.allocated_bytes = sizeof(plan) + plan.objects.capacity() * sizeof(ObjectInit) +
        plan.definitions.capacity() * sizeof(analysis::OpId) +
        plan.nested.capacity() * sizeof(std::optional<analysis::UseId>) + plan.staged.capacity();
    plan.work = budget.Work();
    return plan;
  } catch (Error error) { return error;
  } catch (EvaluationError error) {
    return error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
        error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kFlow;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<LiteralPlan, LiteralError> BuildLiteralPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards, const CellEvaluation* cells, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return BuildLiterals(graph, nullptr, budget, guards, projected, receiver_guards, cells, failure);
}
std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, analysis::SemanticBudget budget, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards, const CellEvaluation* cells, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return VerifyLiterals(graph, plan, nullptr, budget, guards, projected, receiver_guards, cells, failure);
}
std::variant<LiteralPlan, LiteralError> BuildPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, analysis::SemanticBudget budget,
    Guards guards, Guards projected, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return BuildLiterals(graph, &evaluation, budget, guards, projected, {}, nullptr, failure);
}
std::variant<std::uint64_t, LiteralError> VerifyPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget, Guards guards, Guards projected, EvaluationInputSite* failure) {
  if (failure) *failure = {};
  return VerifyLiterals(graph, plan, &evaluation, budget, guards, projected, {}, nullptr, failure);
}
}  // namespace abcd::source
