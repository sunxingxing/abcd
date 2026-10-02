#include "literal_plan.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "../analysis/value_flow.h"
#include "../analysis/value_edits.h"
#include "evaluation_aliases.h"
#include "evaluation_liveness.h"
#include "phase_corridor.h"
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
void Flow(const analysis::ValueProgram& graph, LiteralBudget& budget, const EvaluationPlan* evaluation) {
  if (evaluation) {
    Require(evaluation->flow == EvaluationFlow::kPhased && !graph.phases.empty() &&
        evaluation->operations.size() == graph.ops.size(), Error::kFlow);
    const auto proof = analysis::VerifyPhasedValueFlow(graph, budget.Remaining());
    if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&proof)) {
      if (*error == analysis::ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == analysis::ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<analysis::PhasedValueFlowProof>(proof).work);
    const auto aliases = VerifyEvaluationAliases(graph, *evaluation, budget.Remaining());
    if (const auto* error = std::get_if<EvaluationError>(&aliases)) {
      if (*error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<std::uint64_t>(aliases));
    const auto elided = VerifyElidedPureEvaluations(graph, *evaluation, budget.Remaining());
    if (const auto* error = std::get_if<EvaluationError>(&elided)) {
      if (*error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kFlow;
    }
    budget.Step(std::get<std::uint64_t>(elided));
    return;
  }
  const auto proof = analysis::VerifyValueFlow(graph, budget.Remaining());
  if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&proof)) {
    if (*error == analysis::ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kFlow;
  }
  budget.Step(std::get<analysis::ValueFlowProof>(proof).work);
}
struct LiteralPosition { std::optional<analysis::WorkBlockId> head; std::size_t order = 0; };
class LiteralPath {
 public:
  LiteralPath(const analysis::ValueProgram& graph, const EvaluationPlan* evaluation, LiteralBudget& budget)
      : graph_(graph), evaluation_(evaluation) {
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
    return evaluation_ ? ResolveEvaluationOperand(*evaluation_, operand) : operand;
  }
  bool Same(analysis::WorkBlockId a, analysis::WorkBlockId b) const {
    if (!evaluation_) return a == b;
    return positions_[a.value].head && positions_[a.value].head == positions_[b.value].head;
  }
  bool Before(const analysis::ValueOp& a, const analysis::ValueOp& b) const {
    if (!Same(a.block, b.block)) return false;
    return a.block == b.block ? a.order < b.order : positions_[a.block.value].order < positions_[b.block.value].order;
  }
  bool Alias(analysis::OpId id) const {
    return evaluation_ && evaluation_->operations[id.value].kind == EvaluationKind::kAlias;
  }
  bool Elided(analysis::OpId id) const {
    return evaluation_ && evaluation_->operations[id.value].kind == EvaluationKind::kElidedPure;
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
    return flow.kind != analysis::FlowEdgeKind::kException && Same(last.block, flow.source) &&
        positions_[last.block.value].order <= positions_[flow.source.value].order;
  }
 private:
  const analysis::ValueProgram& graph_;
  const EvaluationPlan* evaluation_;
  std::vector<LiteralPosition> positions_;
};
analysis::OpId Allocation(const analysis::ValueProgram& graph, analysis::OpId id, const LiteralPath& path, bool same_path = true) {
  Require(id.value < graph.ops.size(), Error::kDefinition);
  const auto& op = graph.ops[id.value];
  const auto* write = std::get_if<analysis::ValueOwnWrite>(&op.operation);
  const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&op.operation);
  Require(op.life == analysis::ValueLife::kLive && (write || indexed) && !op.result && op.use_count == 2,
      Error::kDefinition);
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
      (indexed ? std::holds_alternative<analysis::ValueArrayAllocation>(allocation.operation) :
                 std::holds_alternative<analysis::ValueObjectAllocation>(allocation.operation)) &&
      allocation.result == *value && allocation.use_count == 0 &&
      (!same_path || path.Before(allocation, op)), Error::kReceiver);
  return result->op;
}
// Primitive loads have no mutable identity, binding read or coercion. Phased
// integer/float/special loads use ValueCopy; do not admit SSA binding copies.
// Domain and resolved constants remain independently checked by the tree.
bool PrimitiveLeaf(const analysis::ValueProgram& graph, const analysis::ValueOp& op) {
  if (!analysis::CanErasePureOp(op)) return false;
  if (std::holds_alternative<analysis::ValueString>(op.operation)) return op.use_count == 0;
  if (!std::holds_alternative<analysis::ValueCopy>(op.operation) || op.use_count != 1) return false;
  const auto& use = graph.uses[op.first_use.value];
  return use.role == analysis::OperandRole::kInput && use.purpose == analysis::ReadPurpose::kOperand &&
      (std::holds_alternative<analysis::IntegerLiteral>(use.operand) ||
       std::holds_alternative<analysis::Float64Bits>(use.operand) ||
       std::holds_alternative<analysis::SpecialValue>(use.operand));
}
void EarlyValue(const analysis::ValueProgram& graph, const analysis::ValueOp& allocation,
    const analysis::ValueOperand& original, const LiteralPath& path) {
  const auto operand = path.Resolve(original);
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  if (!value) return;
  const auto& definition = graph.values[value->value];
  Require(definition.domain.IsLanguage(), Error::kDomain);
  if (const auto* result = std::get_if<analysis::OpResult>(&definition.origin)) {
    const auto& producer = graph.ops[result->op.value];
    Require(!path.Same(producer.block, allocation.block) || path.Before(producer, allocation), Error::kLateValue);
  } else if (const auto* phi = std::get_if<analysis::PhiDefinition>(&definition.origin)) {
    // Direct mutable phi leaves cannot be moved over an intervening edge copy.
    Require(!path.Phased() || phi->block == allocation.block, Error::kLateValue);
  }
  // Fresh flow proof supplies dominance. A different-block producer available
  // at the same-block own-write is also available at the allocation block entry.
}
bool Aggregate(const analysis::ValueOperation& op) {
  return std::holds_alternative<analysis::ValueArrayAllocation>(op) ||
      std::holds_alternative<analysis::ValueObjectAllocation>(op);
}
bool LiteralUnary(const analysis::ValueOperation& operation) {
  const auto* unary = std::get_if<analysis::ValueUnary>(&operation);
  if (!unary) return false;
  using U = analysis::UnaryOperator;
  switch (unary->op) {
    // These shared AST lowerings evaluate their input exactly once. Domain,
    // coercion/throw order and catch snapshots remain independently checked.
    case U::kNegate: case U::kBitNot: case U::kLogicalNot: case U::kTypeOf:
    case U::kToNumber: case U::kToBoolean: return true;
    // Numeric update/conversion need separate Number-or-BigInt lowering;
    // VM predicates repeat a snapshot leaf, not an owned effect expression.
    case U::kToNumeric: case U::kIncrement: case U::kDecrement:
    case U::kFalseOrZero: case U::kTrueOrNonzeroNumber: return false;
  }
  return false;
}
bool LiteralExpression(const analysis::ValueProgram& graph, const analysis::ValueOp& op, std::size_t index, Guards guards) {
  const auto* creation = std::get_if<analysis::ValueFunctionCreation>(&op.operation);
  const auto* global = std::get_if<analysis::ValueGlobalRead>(&op.operation);
  // Creating a closure does not execute its body. The same literal interval
  // proof retains creation order and forbids intervening environment changes;
  // EvaluationPlan/FamilyTree still own the child and all capture obligations.
  const bool closure = creation && creation->kind == analysis::CreationKind::kFunction &&
      creation->home_object == analysis::HomeObjectInput::kCurrentFunction && op.use_count == 0;
  return PrimitiveLeaf(graph, op) || LiteralUnary(op.operation) ||
      closure ||
      std::holds_alternative<analysis::ValueInvoke>(op.operation) ||
      // Construction is a single-use ordered expression, like a call. The
      // nesting census forbids exposing the unfinished literal to it; shared
      // evaluation/operand checks retain callee, arguments and throw order.
      std::holds_alternative<analysis::ValueConstruct>(op.operation) ||
      std::holds_alternative<analysis::ValueLexicalLoad>(op.operation) ||
      // Name lookup already has a common GlobalNameExpr lowering. Nest only
      // that lookup kind; the same use census, effect/catch interval and final
      // scope checks retain getter throws and forbid repeated/escaped reads.
      (global && global->lookup == analysis::GlobalLookup::kName && op.use_count == 0) ||
      std::holds_alternative<analysis::ValueNamedRead>(op.operation) ||
      std::holds_alternative<analysis::ValuePropertyRead>(op.operation) ||
      (std::holds_alternative<analysis::ValueModuleRead>(op.operation) &&
       index < guards.size() && guards[index]);
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
    std::span<const std::uint8_t> initialized_receivers) {
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
    Require(plan.nested[i]->value < graph.uses.size(), Error::kDefinition);
    const auto* parent = std::get_if<analysis::OpId>(&graph.uses[plan.nested[i]->value].owner);
    Require(parent && parent->value > i && parent->value < graph.ops.size() &&
        path.Before(graph.ops[i], graph.ops[parent->value]), Error::kDefinition);
    value_owners[i] = owners[parent->value] ? owners[parent->value] : value_owners[parent->value];
    Require(value_owners[i] && value_owners[i]->value < i, Error::kDefinition);
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); if (!plan.nested[i]) continue;
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
    if (user && (graph.ops[user->value].life != analysis::ValueLife::kLive || path.Alias(*user) || path.Elided(*user) ||
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
    std::optional<std::size_t> previous;
    for (std::uint32_t u = 0; u < op.use_count; ++u) {
      budget.Step(); const analysis::UseId use{op.first_use.value + u};
      if (ReferenceReceiver(graph, use, path)) continue;
      const auto operand = path.Resolve(graph.uses[use.value].operand);
      const auto* value = std::get_if<analysis::ValueId>(&operand);
      const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
      if (!result) continue;
      if (plan.nested[result->op.value] == use) {
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
    if (op.life != analysis::ValueLife::kLive || path.Alias({static_cast<std::uint32_t>(i)})) continue;
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
    std::span<const std::optional<analysis::OpId>> owners, LiteralBudget& budget) {
  if (plan.staged.empty()) return;
  Require(graph.phases.empty(), Error::kFlow);
  analysis::NormalFlow flow; budget.Size(flow.blocks, graph.blocks.size());
  std::vector<std::size_t> incoming, order, rank;
  budget.Size(incoming, graph.blocks.size()); budget.Size(order, graph.blocks.size()); budget.Size(rank, graph.blocks.size());
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    budget.Step(); auto& output = flow.blocks[b]; const auto& block = graph.blocks[b];
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
  Require(count == graph.blocks.size(), Error::kFlow);
  const auto allowance = budget.Remaining();
  auto built = analysis::BuildNormalDominators(flow, {}, {allowance.bytes, allowance.work});
  if (const auto* error = std::get_if<analysis::DominatorError>(&built)) {
    if (*error == analysis::DominatorError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::DominatorError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kFlow;
  }
  const auto& dom = std::get<analysis::DominatorTree>(built);
  budget.Step(dom.work.steps); budget.Admit(dom.work.allocated_bytes, 1);
  for (const auto& object : plan.objects) {
    budget.Step(); if (object.form != LiteralForm::kStatements) continue;
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
    const auto& final = graph.ops[last.value];
    for (auto at = graph.values[allocation.result->value].first_use; at; at = graph.uses[at->value].next) {
      budget.Step(); const auto& use = graph.uses[at->value];
      if (use.life != analysis::ValueLife::kLive) continue;
      analysis::WorkBlockId block; std::size_t position;
      if (const auto* user = std::get_if<analysis::OpId>(&use.owner)) {
        const auto& op = graph.ops[user->value];
        if (owners[user->value] == object.allocation && *at == op.first_use) continue;
        block = op.block; position = op.order;
      } else {
        Require(use.incoming && std::holds_alternative<analysis::EdgeId>(*use.incoming), Error::kObserved);
        block = graph.edges[std::get<analysis::EdgeId>(*use.incoming).value].source;
        position = graph.blocks[block.value].op_count;
      }
      Require(dom.Dominates({final.block.value}, {block.value}) &&
          (final.block != block || final.order < position), Error::kObserved);
    }
  }
}
struct FieldOwner { analysis::OpId allocation; analysis::OpId definition; };
std::variant<std::uint64_t, LiteralError> VerifyLiterals(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, const EvaluationPlan* evaluation,
    analysis::SemanticBudget allowance, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards) {
  try {
    LiteralBudget budget(allowance);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Flow(graph, budget, evaluation);
    Require(guards.empty() || guards.size() == graph.ops.size(), Error::kDefinition);
    Require(projected.empty() || projected.size() == graph.ops.size(), Error::kDefinition);
    for (std::size_t i = 0; i < guards.size(); ++i) {
      budget.Step();
      if (guards[i]) Require(guards[i]->value < graph.ops.size() &&
          guards[guards[i]->value] == analysis::OpId{static_cast<std::uint32_t>(i)}, Error::kDefinition);
    }
    LiteralPath path(graph, evaluation, budget);
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
        Require(Allocation(graph, id, path, object.form == LiteralForm::kExpression) == object.allocation && !owners[id.value], Error::kCoverage);
        Require(IsStagedLiteral(&plan, id) == (object.form == LiteralForm::kStatements), Error::kCoverage);
        const auto& op = graph.ops[id.value];
        if (const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&op.operation)) {
          const auto& array = std::get<analysis::ValueArrayAllocation>(allocation.operation);
          // Buffered primitives may be replaced, but RHS evaluations must stay
          // in increasing element order. Constants/AST validation checks gaps.
          Require(array.literal ? (!previous_index || indexed->index > *previous_index) :
              indexed->index == i, Error::kDefinition);
          previous_index = indexed->index;
        }
        if (object.form == LiteralForm::kStatements) {
          Require(!evaluation && (plan.nested.empty() || !plan.nested[id.value]), Error::kDefinition);
          owners[id.value] = object.allocation; continue;
        }
        Require(path.Before(graph.ops[prior.value], op), Error::kDefinition); prior = id;
        const analysis::UseId rhs{op.first_use.value + 1};
        const auto operand = path.Resolve(graph.uses[rhs.value].operand);
        const auto* value = std::get_if<analysis::ValueId>(&operand);
        const auto* result = value ? std::get_if<analysis::OpResult>(&graph.values[value->value].origin) : nullptr;
        const bool nested = result && !plan.nested.empty() && plan.nested[result->op.value] == rhs;
        if (nested) Require(path.Before(allocation, graph.ops[result->op.value]), Error::kLateValue);
        else EarlyValue(graph, allocation, operand, path);
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
    VerifyStaged(graph, plan, owners, budget);
    const auto initialized_receivers = InitializedReceiverChecks(graph, receiver_guards, budget, evaluation);
    VerifyNesting(graph, plan, owners, budget, guards, path, evaluation, initialized_receivers);
    if (evaluation) {
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
        if (const auto* op = std::get_if<analysis::OpId>(&use.owner)) {
          if (graph.ops[op->value].life != analysis::ValueLife::kLive || path.Alias(*op) || path.Elided(*op)) continue;
          if (owners[op->value] == object.allocation && graph.ops[op->value].first_use.value == i) continue;
        }
        const auto last = plan.definitions[object.first + std::size_t(object.count) - 1];
        Require(path.After(graph.ops[last.value], use), Error::kObserved);
      }
      return budget.Work();
    }
    for (const auto& object : plan.objects) {
      budget.Step();
      if (object.form == LiteralForm::kStatements) continue;
      const auto& allocation = graph.ops[object.allocation.value];
      const auto last = graph.ops[plan.definitions[object.first + static_cast<std::size_t>(object.count) - 1].value].order;
      auto use = graph.values[allocation.result->value].first_use;
      while (use) {
        budget.Step();
        const auto& occurrence = graph.uses[use->value];
        if (occurrence.life == analysis::ValueLife::kLive) {
          const auto* op = std::get_if<analysis::OpId>(&occurrence.owner);
          if (op && graph.ops[op->value].block == allocation.block) {
            const bool own_base = owners[op->value] == object.allocation &&
                *use == graph.ops[op->value].first_use;
            Require(own_base || graph.ops[op->value].order > last, Error::kObserved);
          }
        }
        use = occurrence.next;
      }
    }
    return budget.Work();
  } catch (Error error) { return error;
  } catch (EvaluationError error) {
    return error == EvaluationError::kResourceLimit ? Error::kResourceLimit :
        error == EvaluationError::kWorkLimit ? Error::kWorkLimit : Error::kFlow;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<LiteralPlan, LiteralError> BuildLiterals(
    const analysis::ValueProgram& graph, const EvaluationPlan* evaluation, analysis::SemanticBudget allowance, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards) {
  try {
    LiteralBudget budget(allowance);
    Flow(graph, budget, evaluation);
    Require(guards.empty() || guards.size() == graph.ops.size(), Error::kDefinition);
    Require(projected.empty() || projected.size() == graph.ops.size(), Error::kDefinition);
    for (std::size_t i = 0; i < guards.size(); ++i) {
      budget.Step();
      if (guards[i]) Require(guards[i]->value < graph.ops.size() &&
          guards[guards[i]->value] == analysis::OpId{static_cast<std::uint32_t>(i)}, Error::kDefinition);
    }
    LiteralPath path(graph, evaluation, budget);
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
      fields[next++] = {Allocation(graph, id, path, evaluation != nullptr), id};
    }
    std::sort(fields.begin(), fields.end(), [&](const auto& a, const auto& b) {
      budget.Step();
      return a.allocation != b.allocation ? a.allocation < b.allocation : a.definition < b.definition;
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
        if (!path.Before(graph.ops[object.allocation.value], graph.ops[id.value])) object.form = LiteralForm::kStatements;
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
          Require(Aggregate(graph.ops[result->op.value].operation) ||
              LiteralExpression(graph, graph.ops[result->op.value], result->op.value, guards), Error::kLateValue);
          Require(!plan.nested[result->op.value], Error::kObserved);
          plan.nested[result->op.value] = use;
        }
      }
    }
    const auto verified = VerifyLiterals(graph, plan, evaluation, budget.Remaining(), guards, projected, receiver_guards);
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
    std::span<const analysis::OpId> receiver_guards) {
  return BuildLiterals(graph, nullptr, budget, guards, projected, receiver_guards);
}
std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, analysis::SemanticBudget budget, Guards guards, Guards projected,
    std::span<const analysis::OpId> receiver_guards) {
  return VerifyLiterals(graph, plan, nullptr, budget, guards, projected, receiver_guards);
}
std::variant<LiteralPlan, LiteralError> BuildPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, analysis::SemanticBudget budget,
    Guards guards) {
  return BuildLiterals(graph, &evaluation, budget, guards, {}, {});
}
std::variant<std::uint64_t, LiteralError> VerifyPhasedLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget, Guards guards) {
  return VerifyLiterals(graph, plan, &evaluation, budget, guards, {}, {});
}
}  // namespace abcd::source
