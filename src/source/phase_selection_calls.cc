#include "logical_evaluation.h"
#include "logical_regions.h"
#include "phase_corridor.h"

#include <algorithm>

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool value) { if (!value) throw EvaluationError::kOwnership; }
bool Pure(const ValueOp& op) {
  return op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
      op.exceptional == ExceptionalCompletion::kImpossible;
}
class CallSelections {
 public:
  CallSelections(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget)
      : graph_(graph), plan_(plan), budget_(budget), phase_budget_(budget.Remaining()),
        corridors_(graph, plan, phase_budget_) {
    budget.Admit(phase_budget_.Cost().bytes, 1);
    accounted_work_ = phase_budget_.Cost().work; budget.Step(accounted_work_);
    budget.Array(uses_, graph.values.size()); budget.Array(incoming_, graph.blocks.size());
    budget.Array(copied_, graph.edges.size()); budget.Array(phi_arguments_, graph.ops.size());
    for (const auto& edge : graph.edges) {
      budget.Step(); if (edge.kind != FlowEdgeKind::kException) ++incoming_[edge.target.value];
    }
    if (plan.phis) for (const auto& bundle : plan.phis->bundles) {
      budget.Step(); if (const auto* edge = std::get_if<EdgeId>(&bundle.incoming)) copied_[edge->value] = bundle.count != 0;
    }
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step(); const auto& use = graph.uses[i];
      if (use.life != ValueLife::kLive) continue;
      if (const auto* op = std::get_if<OpId>(&use.owner)) {
        const auto kind = BeforeLogicalFusion(graph, plan, op->value).kind;
        if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure) continue;
      }
      const auto operand = ResolveEvaluationOperand(plan, use.operand);
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        auto& uses = uses_[value->value];
        if (uses.first) uses.multiple = true;
        else uses.first = UseId{static_cast<std::uint32_t>(i)};
        if (use.role == OperandRole::kArgument) {
          if (const auto* owner = std::get_if<OpId>(&use.owner)) {
            auto input = operand;
            if (const auto* producer = std::get_if<OpResult>(&graph.values[value->value].origin)) {
              const auto& op = graph.ops[producer->op.value];
              if (std::holds_alternative<ValueCopy>(op.operation) && op.use_count == 1)
                input = ResolveEvaluationOperand(plan, graph.uses[op.first_use.value].operand);
            }
            if (const auto* phi = std::get_if<ValueId>(&input);
                phi && std::holds_alternative<PhiDefinition>(graph.values[phi->value].origin)) ++phi_arguments_[owner->value];
          }
        }
      }
    }
  }
  std::optional<PhaseSelectionCall> Check(const SelectionRegion& region) {
    const auto& users = uses_[region.merged.value];
    if (!users.first || users.multiple) return {};
    auto argument = *users.first;
    std::optional<OpId> transport;
    if (const auto* owner = std::get_if<OpId>(&graph_.uses[argument.value].owner)) {
      const auto& op = graph_.ops[owner->value];
      if (std::holds_alternative<ValueCopy>(op.operation)) {
        if (!Pure(op) || !op.result || op.use_count != 1 || op.first_use != argument ||
            BeforeLogicalFusion(graph_, plan_, owner->value).kind != EvaluationKind::kRoot) return {};
        const auto& next = uses_[op.result->value];
        if (!next.first || next.multiple) return {};
        transport = *owner; argument = *next.first;
      }
    }
    const auto& use = graph_.uses[argument.value];
    const auto* call = std::get_if<OpId>(&use.owner);
    // Reject multi-selection calls before any path walk: otherwise many
    // arguments sharing one callee could repeatedly scan the same long prefix.
    if (!call || use.role != OperandRole::kArgument || phi_arguments_[call->value] != 1) return {};
    const auto& invoke = graph_.ops[call->value];
    const auto* operation = std::get_if<ValueInvoke>(&invoke.operation);
    if (!operation || operation->receiver != InvokeReceiver::kExplicit ||
        BeforeLogicalFusion(graph_, plan_, call->value).kind != EvaluationKind::kRoot) return {};
    const auto callee_value = ResolveEvaluationOperand(plan_, graph_.uses[invoke.first_use.value].operand);
    const auto* value = std::get_if<ValueId>(&callee_value);
    const auto* result = value ? std::get_if<OpResult>(&graph_.values[value->value].origin) : nullptr;
    if (!result || !result->commit || uses_[value->value].multiple || uses_[value->value].first != invoke.first_use) return {};
    const auto callee = result->op; const auto& property = graph_.ops[callee.value];
    if ((!std::holds_alternative<ValueNamedRead>(property.operation) &&
         !std::holds_alternative<ValuePropertyRead>(property.operation)) ||
        BeforeLogicalFusion(graph_, plan_, callee.value).kind != EvaluationKind::kRoot ||
        callee.value >= region.branch.value || region.branch.value >= call->value ||
        corridors_.HasCatchCopies(callee) || corridors_.HasCatchCopies(*call) ||
        !corridors_.SameCatch(callee, *call)) return {};
    // The callee reference encloses the selection. Prefix/tail operations may
    // not observe its delayed publication or perform independent effects.
    // Keep computed prefix snapshots unsupported until they have an explicit
    // expression owner; never replay a getter or dispatch through .call.
    if (!Inert(*result->commit, region.header) || !Inert(region.join, invoke.block, true)) return {};
    for (const auto* arm : {&region.right, &*region.consequent}) {
      const auto* input = std::get_if<ValueId>(arm);
      const auto* producer = input ? std::get_if<OpResult>(&graph_.values[input->value].origin) : nullptr;
      if (!producer) return {};
      const auto& op = graph_.ops[producer->op.value];
      if (op.exceptional == ExceptionalCompletion::kMayThrow &&
          !corridors_.SameCatch(producer->op, *call)) return {};
    }
    return PhaseSelectionCall{callee, *call, region.branch, region.merged, argument, transport};
  }
  void Account() {
    budget_.Step(phase_budget_.Cost().work - accounted_work_);
  }
 private:
  bool Inert(WorkBlockId at, WorkBlockId end, bool join_entry = false) {
    for (std::size_t visited = 0; visited < graph_.blocks.size(); ++visited) {
      budget_.Step(); if (at == end) return true;
      if ((incoming_[at.value] != 1 && !(join_entry && visited == 0)) || graph_.phases[at.value].phase == ExceptionPhase::kCatch) return false;
      const auto& block = graph_.blocks[at.value];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget_.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph_.ops[id.value];
        if (op.life == ValueLife::kLive && (!Pure(op) || corridors_.HasCatchCopies(id))) return false;
      }
      std::optional<EdgeId> normal;
      for (std::uint32_t i = 0; i < block.edge_count; ++i) {
        budget_.Step(); const EdgeId edge{block.first_edge.value + i};
        if (graph_.edges[edge.value].kind == FlowEdgeKind::kException) continue;
        if (normal || copied_[edge.value]) return false;
        normal = edge;
      }
      if (!normal) return false;
      at = graph_.edges[normal->value].target;
    }
    return false;
  }
  const ValueProgram& graph_;
  const EvaluationPlan& plan_;
  TreeCheckBudget& budget_;
  PhaseBudget phase_budget_;
  PhaseCorridors corridors_;
  std::uint64_t accounted_work_ = 0;
  std::vector<PhaseUse> uses_;
  std::vector<std::uint32_t> incoming_, phi_arguments_;
  std::vector<std::uint8_t> copied_;
};
}
void BuildPhaseSelectionCalls(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  if (regions.empty()) return;
  CallSelections proof(graph, plan, budget);
  std::vector<PhaseSelectionCall> calls;
  budget.Admit(regions.size(), sizeof(PhaseSelectionCall)); calls.reserve(regions.size());
  budget.Admit(calls.capacity() - regions.size(), sizeof(PhaseSelectionCall));
  for (const auto& region : regions) {
    budget.Step(); if (const auto call = proof.Check(region)) calls.push_back(*call);
  }
  proof.Account();
  if (calls.empty()) return;
  std::sort(calls.begin(), calls.end(), [&](const auto& a, const auto& b) { budget.Step(); return a.callee < b.callee; });
  budget.Array(plan.logical_values, graph.values.size());
  budget.Array(plan.phase_transports, graph.ops.size());
  for (std::size_t i = 0; i < calls.size(); ++i) {
    budget.Step(); const auto& call = calls[i];
    Require(!i || calls[i - 1].callee < call.callee);
    Require(!plan.logical_values[call.value.value]);
    plan.logical_values[call.value.value] = call.branch;
    const auto input = call.transport ? graph.ops[call.transport->value].first_use : call.argument;
    plan.operations[call.branch.value] = {EvaluationKind::kLogicalValue, input, {}};
    if (call.transport) {
      Require(!plan.phase_transports[call.transport->value]);
      plan.phase_transports[call.transport->value] = call.argument;
      plan.operations[call.transport->value] = {EvaluationKind::kInline, call.argument, {}};
    }
    plan.operations[call.callee.value] = {EvaluationKind::kInline, graph.ops[call.call.value].first_use, {}};
  }
  plan.phase_calls = std::move(calls);
}
void VerifyPhaseSelectionCalls(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  Require(plan.logical_objects.empty());
  Require(plan.logical_values.empty() || plan.logical_values.size() == graph.values.size());
  if (plan.phase_calls.empty()) {
    Require(plan.logical_values.empty() && plan.phase_transports.empty());
    for (const auto& op : plan.operations) { budget.Step(); Require(op.kind != EvaluationKind::kLogicalValue); }
    return;
  }
  Require(!plan.logical_values.empty() && plan.phase_transports.size() == graph.ops.size());
  for (std::size_t i = 0; i < plan.phase_calls.size(); ++i) {
    budget.Step(); const auto& call = plan.phase_calls[i];
    Require(call.callee.value < graph.ops.size() && (!i || plan.phase_calls[i - 1].callee < call.callee));
  }
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  CallSelections proof(graph, plan, budget);
  std::vector<std::uint8_t> values, calls, transports, branches;
  budget.Array(values, graph.values.size()); budget.Array(calls, plan.phase_calls.size());
  budget.Array(transports, graph.ops.size()); budget.Array(branches, graph.ops.size());
  for (const auto& region : regions) {
    budget.Step(); if (!plan.logical_values[region.merged.value]) continue;
    const auto expected = proof.Check(region); Require(expected.has_value());
    const auto at = std::lower_bound(plan.phase_calls.begin(), plan.phase_calls.end(), expected->callee,
        [&](const auto& entry, auto id) { budget.Step(); return entry.callee < id; });
    Require(at != plan.phase_calls.end() && *at == *expected);
    const auto index = static_cast<std::size_t>(at - plan.phase_calls.begin());
    Require(!calls[index] && !values[region.merged.value]); calls[index] = 1; values[region.merged.value] = 1;
    Require(plan.logical_values[region.merged.value] == region.branch);
    const auto& branch = plan.operations[region.branch.value]; const auto& callee = plan.operations[expected->callee.value];
    const auto input = expected->transport ? graph.ops[expected->transport->value].first_use : expected->argument;
    branches[region.branch.value] = 1;
    if (expected->transport) {
      const auto op = expected->transport->value;
      Require(!transports[op] && plan.phase_transports[op] == expected->argument &&
          plan.operations[op].kind == EvaluationKind::kInline &&
          plan.operations[op].consumer == expected->argument && !plan.operations[op].temporary);
      transports[op] = 1;
    }
    Require(branch.kind == EvaluationKind::kLogicalValue && branch.consumer == input && !branch.temporary &&
        callee.kind == EvaluationKind::kInline && callee.consumer == graph.ops[expected->call.value].first_use && !callee.temporary);
  }
  proof.Account();
  for (std::size_t i = 0; i < values.size(); ++i) { budget.Step(); Require(plan.logical_values[i].has_value() == (values[i] != 0)); }
  for (const auto value : calls) { budget.Step(); Require(value != 0); }
  for (std::size_t i = 0; i < transports.size(); ++i) {
    budget.Step(); Require(plan.phase_transports[i].has_value() == (transports[i] != 0));
    Require((plan.operations[i].kind == EvaluationKind::kLogicalValue) == (branches[i] != 0));
  }
}
}  // namespace abcd::source
