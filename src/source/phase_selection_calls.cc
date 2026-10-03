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
  CallSelections(const ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget,
      std::span<const SelectionRegion> regions)
      : graph_(graph), plan_(plan), budget_(budget), phase_budget_(budget.Remaining()),
        corridors_(graph, plan, phase_budget_) {
    budget.Admit(phase_budget_.Cost().bytes, 1);
    accounted_work_ = phase_budget_.Cost().work; budget.Step(accounted_work_);
    budget.Array(uses_, graph.values.size()); budget.Array(incoming_, graph.blocks.size());
    budget.Array(copied_, graph.edges.size()); budget.Array(phi_arguments_, graph.ops.size());
    budget.Array(callees_, graph.values.size()); budget.Array(prefix_, graph.ops.size());
    budget.Array(heads_, graph.values.size()); budget.Array(next_, graph.uses.size());
    budget.Array(roots_, graph.ops.size()); budget.Array(owners_, graph.ops.size());
    budget.Array(root_selections_, graph.ops.size());
    for (std::size_t i = graph.ops.size(); i-- > 0;) {
      budget.Step(); roots_[i] = OpId{static_cast<std::uint32_t>(i)};
      const auto placement = BeforeLogicalFusion(graph, plan, i);
      if (placement.kind != EvaluationKind::kInline || !placement.consumer) continue;
      if (placement.consumer->value >= graph.uses.size()) throw EvaluationError::kOwnership;
      if (const auto* owner = std::get_if<OpId>(&graph.uses[placement.consumer->value].owner);
          owner && owner->value > i) roots_[i] = roots_[owner->value];
    }
    for (const auto& module : plan.modules) {
      budget.Step(); if (module.pair.guard) roots_[module.pair.guard->value] = roots_[module.pair.read.value];
    }
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
        if (use.role == OperandRole::kCallee) {
          if (const auto* owner = std::get_if<OpId>(&use.owner)) {
            const auto* invoke = std::get_if<ValueInvoke>(&graph.ops[owner->value].operation);
            if (invoke && invoke->receiver == InvokeReceiver::kExplicit) callees_[value->value] = 1;
          }
        }
        next_[i] = heads_[value->value]; heads_[value->value] = UseId{static_cast<std::uint32_t>(i)};
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
    for (const auto& region : regions) {
      budget.Step(); const auto& user = uses_[region.merged.value];
      if (!user.first || user.multiple) continue;
      auto use = *user.first;
      const auto* owner = std::get_if<OpId>(&graph.uses[use.value].owner);
      if (owner && std::holds_alternative<ValueCopy>(graph.ops[owner->value].operation)) {
        const auto& copy = graph.ops[owner->value];
        if (!copy.result || !uses_[copy.result->value].first || uses_[copy.result->value].multiple) continue;
        use = *uses_[copy.result->value].first;
        owner = std::get_if<OpId>(&graph.uses[use.value].owner);
      }
      if (owner && graph.uses[use.value].role == OperandRole::kArgument)
        ++root_selections_[roots_[owner->value].value];
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
    const auto call_kind = BeforeLogicalFusion(graph_, plan_, call->value).kind;
    if (!operation || operation->receiver != InvokeReceiver::kExplicit ||
        (call_kind != EvaluationKind::kRoot && call_kind != EvaluationKind::kInline)) return {};
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
    // Prefix roots retain their snapshots, in original execution order inside
    // the condition. The callee is evaluated first; normal-path copies and
    // pre-throw staging remain barriers. A different saved method reference
    // stops the walk, preventing overlapping ownership and repeated long scans.
    std::size_t prefix_count = 0;
    if (!Prefix(*result->commit, region.header, *call, region.branch, prefix_count) ||
        !Inert(region.join, invoke.block, true)) return {};
    for (std::size_t i = 0; i < prefix_count; ++i) owners_[prefix_[i].value] = region.branch;
    for (const auto& condition : region.conditions) owners_[condition.branch.value] = region.branch;
    for (const auto* arm : {&region.right, &*region.consequent}) {
      // Snapshot inputs have no producer owned by the selected arm. Discovery
      // preserves their inert path, original edge use and available binding.
      if (std::holds_alternative<SelectionSnapshot>(*arm)) continue;
      const auto* input = std::get_if<ValueId>(arm);
      const auto* producer = input ? std::get_if<OpResult>(&graph_.values[input->value].origin) : nullptr;
      if (!producer) return {};
      owners_[producer->op.value] = region.branch;
      const auto& op = graph_.ops[producer->op.value];
      if (op.exceptional == ExceptionalCompletion::kMayThrow &&
          !corridors_.SameCatch(producer->op, *call)) return {};
    }
    // A moved prefix snapshot may be consumed only by this prefix, scalar
    // decision or selected arms. In particular an earlier argument of an
    // enclosing call must not read it before the inner call evaluates it.
    for (std::size_t i = 0; i < prefix_count; ++i) {
      budget_.Step(); const auto value = *graph_.ops[prefix_[i].value].result;
      for (auto at = heads_[value.value]; at; at = next_[at->value]) {
        budget_.Step(); const auto& use = graph_.uses[at->value];
        if (const auto* owner = std::get_if<OpId>(&use.owner);
            owner && owners_[roots_[owner->value].value] == region.branch) continue;
        // A retained selected-arm input is an actual observation on this
        // region's phi edge. It belongs to the same argument expression, not
        // to an external phi or an enclosing call's earlier argument.
        const auto* merged = std::get_if<ValueId>(&use.owner);
        const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
        if (!merged || *merged != region.merged || use.role != OperandRole::kPhiInput ||
            !edge || (*edge != region.keep_input && *edge != region.evaluate_input)) return {};
      }
    }
    PhaseSelectionCall selected{callee, *call, region.branch, region.merged, argument, transport};
    budget_.Array(selected.prefix, prefix_count);
    std::copy_n(prefix_.begin(), prefix_count, selected.prefix.begin());
    Ancestors(selected);
    return selected;
  }
  void Account() {
    budget_.Step(phase_budget_.Cost().work - accounted_work_);
  }
 private:
  bool ReferencePrefix(WorkBlockId at, WorkBlockId end, OpId callee, OpId call) {
    for (std::size_t visited = 0; visited < graph_.blocks.size(); ++visited) {
      budget_.Step(); if (at == end) return true;
      if (incoming_[at.value] != 1 || graph_.phases[at.value].phase == ExceptionPhase::kCatch) return false;
      const auto& block = graph_.blocks[at.value];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget_.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        if (corridors_.HasCatchCopies(id)) return false;
        if (Pure(op)) continue;
        // The child member reference owns these original effect dependencies.
        // Other roots/earlier effectful arguments cannot move past the parent.
        if (roots_[id.value] != callee ||
            (op.exceptional == ExceptionalCompletion::kMayThrow && !corridors_.SameCatch(id, call))) return false;
      }
      std::optional<EdgeId> next;
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        budget_.Step(); const EdgeId edge{block.first_edge.value + e};
        if (graph_.edges[edge.value].kind == FlowEdgeKind::kException) continue;
        if (next || copied_[edge.value]) return false;
        next = edge;
      }
      if (!next) return false;
      at = graph_.edges[next->value].target;
    }
    return false;
  }
  void Ancestors(PhaseSelectionCall& selected) {
    auto child_call = selected.call, child_callee = selected.callee;
    // A single selection per original final tree owns disjoint ancestor-prefix
    // segments. Multiple selections would repeatedly scan a shared long prefix
    // and need a joint ordering proof; do not infer one from greedy suffixes.
    if (root_selections_[roots_[child_call.value].value] != 1) return;
    for (;;) {
      budget_.Step(); const auto placement = BeforeLogicalFusion(graph_, plan_, child_call.value);
      if (placement.kind != EvaluationKind::kInline || !placement.consumer) break;
      const auto& input = graph_.uses[placement.consumer->value];
      const auto* parent = std::get_if<OpId>(&input.owner);
      if (!parent || input.role != OperandRole::kArgument) break;
      const auto& call = graph_.ops[parent->value];
      const auto* invoke = std::get_if<ValueInvoke>(&call.operation);
      if (!invoke || invoke->receiver != InvokeReceiver::kExplicit) break;
      const auto operand = ResolveEvaluationOperand(plan_, graph_.uses[call.first_use.value].operand);
      const auto* value = std::get_if<ValueId>(&operand);
      const auto* source = value ? std::get_if<OpResult>(&graph_.values[value->value].origin) : nullptr;
      if (!source || !source->commit || source->op.value >= child_callee.value ||
          uses_[value->value].multiple || uses_[value->value].first != call.first_use) break;
      const auto& property = graph_.ops[source->op.value];
      if ((!std::holds_alternative<ValueNamedRead>(property.operation) &&
           !std::holds_alternative<ValuePropertyRead>(property.operation)) ||
          BeforeLogicalFusion(graph_, plan_, source->op.value).kind != EvaluationKind::kRoot ||
          corridors_.HasCatchCopies(source->op) || corridors_.HasCatchCopies(*parent) ||
          !corridors_.SameCatch(source->op, *parent) || !corridors_.SameCatch(child_call, *parent) ||
          !ReferencePrefix(*source->commit, graph_.ops[child_callee.value].block, child_callee, *parent)) break;
      if (selected.ancestors.size() == selected.ancestors.capacity()) {
        Require(selected.ancestors.size() < graph_.ops.size());
        const auto capacity = selected.ancestors.empty() ? 1 : std::min(graph_.ops.size(), selected.ancestors.size() * 2);
        budget_.Admit(capacity, sizeof(PhaseCallReference)); selected.ancestors.reserve(capacity);
        budget_.Admit(selected.ancestors.capacity() - capacity, sizeof(PhaseCallReference));
      }
      selected.ancestors.push_back({source->op, *parent});
      child_call = *parent; child_callee = source->op;
    }
  }
  bool Prefix(WorkBlockId at, WorkBlockId end, OpId call, OpId branch, std::size_t& count) {
    for (std::size_t visited = 0; visited < graph_.blocks.size(); ++visited) {
      budget_.Step(); if (at == end) return true;
      if (incoming_[at.value] != 1 || graph_.phases[at.value].phase == ExceptionPhase::kCatch) return false;
      const auto& block = graph_.blocks[at.value];
      for (std::uint32_t i = 0; i < block.op_count; ++i) {
        budget_.Step(); const OpId id{block.first_op.value + i}; const auto& op = graph_.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        if (corridors_.HasCatchCopies(id)) return false;
        const auto kind = BeforeLogicalFusion(graph_, plan_, id.value).kind;
        if (kind == EvaluationKind::kAlias || kind == EvaluationKind::kElidedPure ||
            id == branch || std::holds_alternative<ValueJump>(op.operation) || std::holds_alternative<ValueNop>(op.operation)) {
          if (!Pure(op)) return false;
          continue;
        }
        if (op.exceptional == ExceptionalCompletion::kMayThrow && !corridors_.SameCatch(id, call)) return false;
        if (kind == EvaluationKind::kInline || kind == EvaluationKind::kModuleGuard) continue;
        if (kind != EvaluationKind::kRoot || !op.result || !PhaseCorridors::Expression(op.operation) ||
            callees_[op.result->value] || count >= prefix_.size()) return false;
        prefix_[count++] = id;
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
  std::vector<std::uint8_t> copied_, callees_;
  std::vector<OpId> prefix_, roots_;
  std::vector<std::uint32_t> root_selections_;
  std::vector<std::optional<UseId>> heads_, next_;
  std::vector<std::optional<OpId>> owners_;
};
}
void BuildPhaseSelectionCalls(const ValueProgram& graph, EvaluationPlan& plan, TreeCheckBudget& budget) {
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  if (regions.empty()) return;
  CallSelections proof(graph, plan, budget, regions);
  std::vector<PhaseSelectionCall> calls;
  budget.Admit(regions.size(), sizeof(PhaseSelectionCall)); calls.reserve(regions.size());
  budget.Admit(calls.capacity() - regions.size(), sizeof(PhaseSelectionCall));
  for (const auto& region : regions) {
    budget.Step(); if (auto call = proof.Check(region)) calls.push_back(std::move(*call));
  }
  proof.Account();
  if (calls.empty()) return;
  std::sort(calls.begin(), calls.end(), [&](const auto& a, const auto& b) { budget.Step(); return a.callee < b.callee; });
  budget.Array(plan.logical_values, graph.values.size());
  budget.Array(plan.phase_transports, graph.ops.size());
  budget.Array(plan.phase_prefixes, graph.ops.size());
  budget.Array(plan.phase_references, graph.ops.size());
  for (std::size_t i = 0; i < calls.size(); ++i) {
    budget.Step(); const auto& call = calls[i];
    Require(!i || calls[i - 1].callee < call.callee);
    Require(!plan.logical_values[call.value.value]);
    Require(!plan.phase_prefixes[call.callee.value]);
    Require(!plan.phase_references[call.callee.value]);
    plan.phase_references[call.callee.value] = graph.ops[call.call.value].first_use;
    for (const auto& ancestor : call.ancestors) {
      budget.Step(); Require(!plan.phase_references[ancestor.callee.value]);
      plan.phase_references[ancestor.callee.value] = graph.ops[ancestor.call.value].first_use;
      plan.operations[ancestor.callee.value] = {EvaluationKind::kInline, graph.ops[ancestor.call.value].first_use, {}};
    }
    for (const auto prefix : call.prefix) {
      budget.Step(); Require(!plan.phase_prefixes[prefix.value] && plan.operations[prefix.value].kind == EvaluationKind::kRoot);
      plan.phase_prefixes[prefix.value] = call.branch;
    }
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
    Require(plan.logical_values.empty() && plan.phase_transports.empty() && plan.phase_prefixes.empty() && plan.phase_references.empty());
    for (const auto& op : plan.operations) { budget.Step(); Require(op.kind != EvaluationKind::kLogicalValue); }
    return;
  }
  Require(!plan.logical_values.empty() && plan.phase_transports.size() == graph.ops.size() &&
      plan.phase_prefixes.size() == graph.ops.size() && plan.phase_references.size() == graph.ops.size());
  for (std::size_t i = 0; i < plan.phase_calls.size(); ++i) {
    budget.Step(); const auto& call = plan.phase_calls[i];
    Require(call.callee.value < graph.ops.size() && (!i || plan.phase_calls[i - 1].callee < call.callee));
  }
  const auto regions = FindPhasedPhiSelections(graph, plan, budget);
  CallSelections proof(graph, plan, budget, regions);
  std::vector<std::uint8_t> values, calls, transports, branches, prefixes;
  budget.Array(values, graph.values.size()); budget.Array(calls, plan.phase_calls.size());
  budget.Array(transports, graph.ops.size()); budget.Array(branches, graph.ops.size());
  budget.Array(prefixes, graph.ops.size());
  std::vector<std::uint8_t> references; budget.Array(references, graph.ops.size());
  for (const auto& region : regions) {
    budget.Step(); if (!plan.logical_values[region.merged.value]) continue;
    const auto expected = proof.Check(region); Require(expected.has_value());
    const auto at = std::lower_bound(plan.phase_calls.begin(), plan.phase_calls.end(), expected->callee,
        [&](const auto& entry, auto id) { budget.Step(); return entry.callee < id; });
    Require(at != plan.phase_calls.end());
    budget.Step(at->prefix.size()); budget.Step(at->ancestors.size());
    Require(*at == *expected);
    const auto index = static_cast<std::size_t>(at - plan.phase_calls.begin());
    const auto reference = [&](PhaseCallReference binding) {
      budget.Step(); const auto callee = binding.callee.value;
      Require(!references[callee] && plan.phase_references[callee] == graph.ops[binding.call.value].first_use &&
          plan.operations[callee].kind == EvaluationKind::kInline &&
          plan.operations[callee].consumer == plan.phase_references[callee] && !plan.operations[callee].temporary);
      references[callee] = 1;
    };
    reference({expected->callee, expected->call});
    for (const auto& ancestor : expected->ancestors) reference(ancestor);
    Require(!calls[index] && !values[region.merged.value]); calls[index] = 1; values[region.merged.value] = 1;
    Require(plan.logical_values[region.merged.value] == region.branch);
    const auto& branch = plan.operations[region.branch.value]; const auto& callee = plan.operations[expected->callee.value];
    const auto input = expected->transport ? graph.ops[expected->transport->value].first_use : expected->argument;
    branches[region.branch.value] = 1;
    for (const auto prefix : expected->prefix) {
      budget.Step(); Require(!prefixes[prefix.value] && plan.phase_prefixes[prefix.value] == region.branch &&
          plan.operations[prefix.value].kind == EvaluationKind::kRoot);
      prefixes[prefix.value] = 1;
    }
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
    Require(plan.phase_prefixes[i].has_value() == (prefixes[i] != 0));
    Require(plan.phase_references[i].has_value() == (references[i] != 0));
    Require((plan.operations[i].kind == EvaluationKind::kLogicalValue) == (branches[i] != 0));
  }
}
}  // namespace abcd::source
