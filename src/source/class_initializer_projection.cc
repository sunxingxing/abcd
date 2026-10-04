#include "class_initializers.h"
#include "class_self_order.h"
#include "effect_order.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<ClassProjection, ClassInitializerError> ProjectClassMembers(
    const analysis::ValueProgram& graph, std::span<const ClassInitializerCall> requested,
    analysis::SemanticBudget budget, std::span<const ClassDefinitionSite> definitions, const ClassSelfOrder* self_order) {
  using namespace analysis;
  using Error = ClassInitializerError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error = Error::kInput) { if (!ok) throw error; };
    const auto step = [&](std::size_t n = 1) {
      require(n <= budget.work, Error::kWorkLimit); budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      require(n <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= n * width;
    };
    ClassProjection result;
    auto member_result = ProjectClassDefinitions(graph, definitions, budget);
    if (const auto* error = std::get_if<ClassDefinitionError>(&member_result)) {
      switch (*error) {
        case ClassDefinitionError::kInput: return Error::kInput;
        case ClassDefinitionError::kFlow: return Error::kFlow;
        case ClassDefinitionError::kResourceLimit: return Error::kResourceLimit;
        case ClassDefinitionError::kWorkLimit: return Error::kWorkLimit;
      }
    }
    auto member_projection = std::get<ClassDefinitionProjection>(std::move(member_result));
    admit(member_projection.allocated_bytes, 1); step(member_projection.work);
    result.owners = std::move(member_projection.owners);
    result.prototypes = std::move(member_projection.prototypes);
    if (requested.empty()) {
      result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
      return result;
    }
    const auto available = BuildEffectOrder(graph,budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&available)) {
      if (*error == ValueAvailabilityError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    const auto& order = std::get<EffectOrder>(available);
    admit(order.allocated_bytes, 1); step(order.work);
    auto found = FindClassInitializerCalls(graph, budget);
    if (const auto* error = std::get_if<Error>(&found)) return *error;
    const auto& candidates = std::get<ClassInitializerCalls>(found);
    admit(candidates.allocated_bytes, 1); step(candidates.work);
    // The same effect-predecessor identities used by member definitions prove
    // one private creation-to-call chain. Pure CFG splits are transparent;
    // different histories, repeated calls and early escapes cannot be fused.
    // Regression: class_initializers_test cross-initializer decoded cases.
    admit(graph.ops.size(), sizeof(std::optional<OpId>));
    std::vector<std::optional<OpId>> effects(graph.ops.size());
    admit(effects.capacity() - effects.size(), sizeof(std::optional<OpId>));
    admit(graph.ops.size(), sizeof(std::uint8_t));
    std::vector<std::uint8_t> successors(graph.ops.size());
    admit(successors.capacity() - successors.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::uint8_t));
    std::vector<std::uint8_t> claimed(graph.ops.size());
    admit(claimed.capacity() - claimed.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::optional<OpId>));
    result.owners.resize(graph.ops.size());
    admit(result.owners.capacity() - result.owners.size(), sizeof(std::optional<OpId>));
    admit(graph.ops.size(), sizeof(OpId)); result.prototypes.reserve(graph.ops.size());
    admit(result.prototypes.capacity() - graph.ops.size(), sizeof(OpId));
    std::vector<std::optional<ClassSelfStore>> self_stores;
    if (self_order) {
      const auto stores = self_order->Stores(graph);
      require(!stores.empty(),Error::kInput);
      admit(graph.ops.size(),sizeof(std::optional<ClassSelfStore>)); step(graph.ops.size()); self_stores.resize(graph.ops.size());
      admit(self_stores.capacity()-self_stores.size(),sizeof(std::optional<ClassSelfStore>));
      for (const auto& store : stores) { step(); require(store.store.value < self_stores.size() && !self_stores[store.store.value]); self_stores[store.store.value] = store; }
    }
    std::optional<OpId> previous;
    for (const auto& call : requested) {
      step(); require(!previous || *previous < call.method); previous = call.method;
      const auto actual = std::lower_bound(candidates.calls.begin(), candidates.calls.end(), call.method,
          [&](const auto& candidate, auto method) { step(); return candidate.method < method; });
      require(actual != candidates.calls.end() && *actual == call);
      require(!claimed[call.creation.value]);
      claimed[call.creation.value] = 1;
      const auto& creation = graph.ops[call.creation.value];
      const auto& invoke = graph.ops[call.call.value];
      const auto project = [&](OpId id) {
        const auto& op = graph.ops[id.value];
        require(order.Before(graph, call.creation, op.block, op.order) &&
            (id == call.call || order.Before(graph, id, invoke.block, invoke.order)), Error::kFlow);
        require(!result.owners[id.value]); result.owners[id.value] = call.creation;
        require(HasOrderedEffect(op), Error::kFlow); effects[id.value] = call.creation;
      };
      project(call.method); project(call.call);
      for (auto use = graph.values[call.receiver.value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value];
        const auto* owner = std::get_if<OpId>(&occurrence.owner);
        if (!owner) {
          require(occurrence.incoming && std::holds_alternative<EdgeId>(*occurrence.incoming), Error::kFlow);
          const auto& edge = graph.edges[std::get<EdgeId>(*occurrence.incoming).value];
          require(order.Before(graph, call.call, edge.source, graph.blocks[edge.source.value].op_count), Error::kFlow);
          continue;
        }
        if (*owner == call.method || *owner == call.call) continue;
        const auto& op = graph.ops[owner->value];
        if (!self_stores.empty() && self_stores[owner->value]) {
          const auto& self = *self_stores[owner->value];
          require(self.creation == call.creation && self.initializer == call.call &&
              std::holds_alternative<ValueLexicalStore>(op.operation) && occurrence.role == OperandRole::kStoredValue,Error::kFlow);
          require(order.Before(graph, call.creation, op.block, op.order) &&
              order.Before(graph, *owner, invoke.block, invoke.order) && !effects[owner->value], Error::kFlow);
          effects[owner->value] = call.creation;
          continue; // Retained cell write, never projected away.
        }
        // Every unowned observation, including a phi's actual incoming edge,
        // must occur after the call on all paths. Numeric placement says nothing
        // about an observer in a bypass arm or an earlier loop iteration.
        if (order.Before(graph, call.call, op.block, op.order)) continue;
        require(std::holds_alternative<ValueNamedRead>(op.operation) && op.use_count == 1 &&
            occurrence.role == OperandRole::kBase && op.result &&
            !graph.values[op.result->value].first_use, Error::kFlow);
        // Named-read spelling remains an explicit source-level obligation.
        project(*owner); result.prototypes.push_back(*owner);
      }
      require(HasOrderedEffect(creation), Error::kFlow);
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); if (!effects[i]) continue;
      const auto* predecessor = std::get_if<OpId>(&order.before[i]);
      require(predecessor && (*predecessor == *effects[i] || effects[predecessor->value] == effects[i]), Error::kFlow);
      require(order.Before(graph, *predecessor, graph.ops[i].block, graph.ops[i].order) &&
          order.after[predecessor->value] == OpId{static_cast<std::uint32_t>(i)} &&
          !successors[predecessor->value], Error::kFlow);
      successors[predecessor->value] = 1;
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
