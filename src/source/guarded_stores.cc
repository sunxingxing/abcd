#include "guarded_stores.h"
#include "effect_order.h"

#include <algorithm>
#include <bit>

namespace abcd::source {
namespace {
using namespace analysis;
void Require(bool ok, GuardedStoreError error) { if (!ok) throw error; }
}
GuardedStores BuildGuardedStores(const ValueProgram& graph,
    const ValueEnvironments& environments, const GuardedReads& reads,
    const ExceptionDispatch& dispatch, TreeCheckBudget& budget) {
  Require(graph.revision == environments.revision && graph.cfg_revision == environments.cfg_revision &&
      graph.revision == reads.revision && graph.cfg_revision == reads.cfg_revision &&
      !graph.phases.empty(), GuardedStoreError::kInput);
  GuardedStores result{graph.revision, graph.cfg_revision};
  auto built = BuildEffectOrder(graph, budget.Remaining());
  if (const auto* e = std::get_if<ValueAvailabilityError>(&built)) {
    if (*e == ValueAvailabilityError::kResourceLimit) throw TreeStructureError::kResourceLimit;
    if (*e == ValueAvailabilityError::kWorkLimit) throw TreeStructureError::kWorkLimit;
    throw GuardedStoreError::kInput;
  }
  const auto& order = std::get<EffectOrder>(built);
  budget.Admit(order.allocated_bytes, 1); budget.Step(order.work);
  std::vector<std::optional<LexicalBinding>> bindings;
  std::vector<std::optional<std::size_t>> guards;
  budget.Array(bindings, graph.ops.size()); budget.Array(guards, graph.ops.size());
  for (const auto& binding : environments.flow.bindings) {
    budget.Step(); Require(binding.instruction < bindings.size() && !bindings[binding.instruction], GuardedStoreError::kInput);
    bindings[binding.instruction] = binding.binding;
  }
  for (std::size_t i = 0; i < reads.reads.size(); ++i) {
    budget.Step(); const auto& read = reads.reads[i];
    Require(read.guard.value < guards.size() && !guards[read.guard.value], GuardedStoreError::kInput);
    guards[read.guard.value] = i;
  }
  budget.Admit(reads.reads.size(), sizeof(GuardedStore)); result.stores.reserve(reads.reads.size());
  budget.Admit(result.stores.capacity() - reads.reads.size(), sizeof(GuardedStore));
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    budget.Step(); const auto& store = graph.ops[i];
    if (store.life != ValueLife::kLive || !std::holds_alternative<ValueLexicalStore>(store.operation)) continue;
    const auto* previous = std::get_if<OpId>(&order.before[i]);
    if (!previous || !guards[previous->value]) continue;
    const auto& read = reads.reads[*guards[previous->value]];
    const OpId id{static_cast<std::uint32_t>(i)};
    if (order.after[read.guard.value] != id || !order.Before(graph, read.guard, store.block, store.order)) continue;
    const auto& from = bindings[read.read.value]; const auto& to = bindings[i];
    if (!from || !to || from->environment != to->environment || from->slot.value != to->slot.value) continue;
    const auto* depth = std::get_if<CapturedDepth>(&to->environment);
    if (!depth) continue; // This proof creates incoming-binding obligations only.
    // No reset, call, environment transition or observable RHS event can hide
    // between the guard and store. Pure copies/block splits are unrestricted.
    Require(store.use_count == 1 && store.exceptional == ExceptionalCompletion::kImpossible &&
        store.origin.value < graph.origins.size(), GuardedStoreError::kInput);
    const auto& input = graph.uses[store.first_use.value];
    Require(input.owner == decltype(input.owner){id} && input.role == OperandRole::kStoredValue, GuardedStoreError::kInput);
    const auto* value = std::get_if<ValueId>(&input.operand);
    if (!value || !graph.values[value->value].domain.IsLanguage()) continue;
    const auto& guard_origin = graph.origins[graph.ops[read.guard.value].origin.value];
    const auto& store_origin = graph.origins[store.origin.value];
    budget.Step(2 * (std::bit_width(dispatch.spans.size()) + 1));
    const auto caught = dispatch.Find(guard_origin.pc);
    if (caught != dispatch.Find(store_origin.pc) || caught !=
        (read.handler ? std::optional{read.handler->dispatch} : std::nullopt)) continue;
    result.stores.push_back({read.read, read.guard, id, *depth, to->slot,
        store.first_use, *value, read.success, read.handler});
  }
  return result;
}
void VerifyGuardedStores(const ValueProgram& graph,
    const ValueEnvironments& environments, const GuardedReads& reads,
    const ExceptionDispatch& dispatch, const GuardedStores& supplied, TreeCheckBudget& budget) {
  Require(supplied.revision == graph.revision && supplied.cfg_revision == graph.cfg_revision, GuardedStoreError::kCorrespondence);
  auto fresh_reads = analysis::ProveGuardedReads(graph, budget.Remaining());
  if (const auto* error = std::get_if<analysis::GuardedReadError>(&fresh_reads)) {
    if (*error == analysis::GuardedReadError::kResourceLimit) throw TreeStructureError::kResourceLimit;
    if (*error == analysis::GuardedReadError::kWorkLimit) throw TreeStructureError::kWorkLimit;
    throw GuardedStoreError::kCorrespondence;
  }
  const auto& guards = std::get<analysis::GuardedReads>(fresh_reads);
  budget.Admit(guards.allocated_bytes, 1); budget.Step(guards.work);
  Require(guards.reads == reads.reads, GuardedStoreError::kCorrespondence);
  auto fresh_environments = analysis::BuildPhasedValueEnvironments(graph, budget.Remaining());
  if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&fresh_environments)) {
    if (*error == analysis::ValueEnvironmentError::kResourceLimit) throw TreeStructureError::kResourceLimit;
    if (*error == analysis::ValueEnvironmentError::kWorkLimit) throw TreeStructureError::kWorkLimit;
    throw GuardedStoreError::kStorage;
  }
  const auto& scopes = std::get<analysis::ValueEnvironments>(fresh_environments);
  budget.Admit(scopes.allocated_bytes, 1); budget.Step(scopes.work);
  const auto expected = BuildGuardedStores(graph, scopes, guards, dispatch, budget);
  Require(expected.stores == supplied.stores, GuardedStoreError::kCorrespondence);
}
}  // namespace abcd::source
