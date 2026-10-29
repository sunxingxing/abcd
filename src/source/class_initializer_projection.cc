#include "class_initializers.h"
#include "../analysis/value_edits.h"
#include "class_self_order.h"
#include "effect_order.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<ClassProjection, ClassInitializerError> ProjectClassMembers(
    const analysis::ValueProgram& graph, std::span<const ClassInitializerCall> requested,
    analysis::SemanticBudget budget, std::span<const ClassDefinitionSite> definitions, const ClassSelfOrder* self_order,
    std::span<const ClassPrototypeRead> prototype_reads) {
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
    // Stored instance initializers are discovered from this same graph, so
    // every projection consumer sees them; class syntax must then print them.
    std::optional<ClassInitializerCalls> discovered;
    if (graph.phases.empty()) {
      auto scanned = FindClassInitializerCalls(graph, budget);
      if (const auto* error = std::get_if<Error>(&scanned)) return *error;
      discovered.emplace(std::get<ClassInitializerCalls>(std::move(scanned)));
      admit(discovered->allocated_bytes, 1); step(discovered->work);
    }
    const std::span<const ClassInstanceInitializer> instances =
        discovered ? std::span<const ClassInstanceInitializer>(discovered->instances) : std::span<const ClassInstanceInitializer>{};
    std::vector<OpId> home_consumers;
    admit(instances.size(), sizeof(OpId)); home_consumers.reserve(instances.size());
    for (const auto& instance : instances) { step(); home_consumers.push_back(instance.method); }
    auto member_result =
        ProjectClassDefinitions(graph, definitions, budget, self_order, home_consumers);
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
    const auto finish = [&]() {
      if (!prototype_reads.empty()) {
        auto discovered = FindClassPrototypeReads(graph, budget);
        if (const auto* error = std::get_if<ClassPrototypeError>(&discovered)) {
          if (*error == ClassPrototypeError::kResourceLimit) throw Error::kResourceLimit;
          if (*error == ClassPrototypeError::kWorkLimit) throw Error::kWorkLimit;
          throw Error::kFlow;
        }
        const auto& candidates = std::get<ClassPrototypeReads>(discovered);
        admit(candidates.allocated_bytes, 1); step(candidates.work);
        if (result.owners.empty()) {
          admit(graph.ops.size(), sizeof(std::optional<OpId>)); result.owners.resize(graph.ops.size());
          admit(result.owners.capacity() - result.owners.size(), sizeof(std::optional<OpId>));
        }
        // Fixed own class prototype: class_info_extractor.cpp installs the
        // nonconfigurable readonly internal accessor and ProtoOrHClass before
        // publishing the class; JSFunction::PrototypeGetter returns that object.
        // Pinned v7 runtime f04900cf. No escape, block or effect-corridor premise
        // applies to this unused read. A live result keeps the original read.
        // Regression: class_prototype_test and paired ViewModel modules.
        const auto capacity = result.prototypes.size() + prototype_reads.size();
        if (capacity > result.prototypes.capacity()) {
          admit(capacity, sizeof(OpId)); result.prototypes.reserve(capacity);
          admit(result.prototypes.capacity() - capacity, sizeof(OpId));
        }
        std::size_t next = 0;
        std::optional<OpId> previous;
        for (const auto& site : prototype_reads) {
          step(); require(!previous || *previous < site.read); previous = site.read;
          while (next < candidates.reads.size() && candidates.reads[next].read < site.read) { step(); ++next; }
          require(next < candidates.reads.size() && candidates.reads[next] == site); ++next;
          const auto& read = graph.ops[site.read.value];
          if (graph.values[read.result->value].first_use) continue;
          auto& owner = result.owners[site.read.value];
          if (owner) { require(*owner == site.creation); continue; }
          owner = site.creation; result.prototypes.push_back(site.read);
        }
      }
      result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
      return std::move(result);
    };
    if (requested.empty() && instances.empty()) {
      return finish();
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
      // The certified installation corridor is effectful. Reuse the existing
      // definition projection, retaining its original operation identities.
      // No ordinary mixed class gains this path without the scoped seal.
      const auto installations = self_order->Installations(graph);
      if (!installations.empty()) {
        admit(graph.ops.size(), sizeof(std::uint8_t));
        std::vector<std::uint8_t> certified(graph.ops.size());
        admit(certified.capacity() - certified.size(), sizeof(std::uint8_t));
        for (const auto& store : stores) {
          step();
          certified[store.creation.value] = 1;
        }
        for (const auto id : installations) {
          step();
          require(id.value < result.owners.size() && result.owners[id.value] &&
                      certified[result.owners[id.value]->value],
                  Error::kFlow);
        }
        for (std::size_t i = 0; i < result.owners.size(); ++i) {
          step();
          if (result.owners[i] && certified[result.owners[i]->value] &&
              HasOrderedEffect(graph.ops[i]))
            effects[i] = result.owners[i];
        }
      }
    }
    // Independently proved member installations are part of the same private
    // construction corridor, whether or not the class has a self cell. The
    // combined predecessor checks below still require every installation to
    // precede the static invocation without an intervening observation.
    for (std::size_t i = 0; i < result.owners.size(); ++i) {
      step();
      if (result.owners[i] && HasOrderedEffect(graph.ops[i]))
        effects[i] = result.owners[i];
    }
    // Stored instance initializers: the prototype read (if otherwise unused),
    // the method and its single slot store join the creation's private
    // corridor. A store of the class value itself (its own binding slot) is a
    // retained cell write inside that corridor, never projected away.
    std::optional<OpId> last_instance;
    for (const auto& instance : instances) {
      step(); require(!last_instance || *last_instance < instance.method); last_instance = instance.method;
      require(instance.creation.value < graph.ops.size() && instance.prototype.value < graph.ops.size() &&
          instance.method.value < graph.ops.size() && instance.store.value < graph.ops.size());
      const auto& creation = graph.ops[instance.creation.value];
      require(std::holds_alternative<ValueClassCreation>(creation.operation) && creation.result, Error::kInput);
      const auto project = [&](OpId id) {
        const auto& op = graph.ops[id.value];
        require(order.Before(graph, instance.creation, op.block, op.order), Error::kFlow);
        require(!result.owners[id.value] || *result.owners[id.value] == instance.creation, Error::kFlow);
        result.owners[id.value] = instance.creation;
        if (HasOrderedEffect(op)) effects[id.value] = instance.creation;
      };
      const auto& read = graph.ops[instance.prototype.value];
      require(read.result.has_value());
      const auto first = graph.values[read.result->value].first_use;
      if (first && !graph.uses[first->value].next && graph.uses[first->value].owner == decltype(graph.uses[first->value].owner){instance.method}) {
        if (!result.owners[instance.prototype.value]) { project(instance.prototype); result.prototypes.push_back(instance.prototype); }
      }
      project(instance.method); project(instance.store);
      for (auto use = graph.values[creation.result->value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto* owner = std::get_if<OpId>(&graph.uses[use->value].owner);
        if (!owner || graph.uses[use->value].role != OperandRole::kStoredValue) continue;
        const auto& op = graph.ops[owner->value];
        if (std::holds_alternative<ValueLexicalStore>(op.operation) && !result.owners[owner->value] &&
            !effects[owner->value] && order.Before(graph, instance.creation, op.block, op.order) &&
            order.Before(graph, *owner, graph.ops[instance.store.value].block, graph.ops[instance.store.value].order))
          effects[owner->value] = instance.creation;
      }
      require(HasOrderedEffect(creation), Error::kFlow);
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
      // No use, or (phased graphs) only dead pure register copies of it.
      const auto unobserved = [&](ValueId value) {
        for (std::size_t hops = 0; hops <= graph.ops.size(); ++hops) {
          step(); const auto first = graph.values[value.value].first_use;
          if (!first) return true;
          const auto& use = graph.uses[first->value];
          const auto* copy_id = std::get_if<OpId>(&use.owner);
          if (graph.phases.empty() || use.next || !copy_id) return false;
          const auto& copy = graph.ops[copy_id->value];
          if (copy.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(copy.operation) ||
              !CanErasePureOp(copy) || !copy.result) return false;
          value = *copy.result;
        }
        return false;
      };
      // Observers of the class value, through pure register copies (phased
      // graphs keep them; normal graphs have none). A copy is no observation;
      // its own uses are. Each copy has one operand, so each use is seen once.
      require(creation.result.has_value());
      std::vector<ValueId> aliases{*creation.result};
      admit(1, sizeof(ValueId));
      for (std::size_t next_alias = 0; next_alias < aliases.size(); ++next_alias)
      for (auto use = graph.values[aliases[next_alias].value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value];
        const auto* owner = std::get_if<OpId>(&occurrence.owner);
        if (owner && graph.ops[owner->value].result && std::holds_alternative<ValueCopy>(graph.ops[owner->value].operation) &&
            !graph.phases.empty()) {
          std::uint64_t hops = 0;
          if (CopyRoot(graph, *graph.ops[owner->value].result, hops) == *creation.result) {
            step(hops); admit(1, sizeof(ValueId)); aliases.push_back(*graph.ops[owner->value].result); continue;
          }
        }
        if (!owner) {
          require(occurrence.incoming && std::holds_alternative<EdgeId>(*occurrence.incoming), Error::kFlow);
          const auto& edge = graph.edges[std::get<EdgeId>(*occurrence.incoming).value];
          require(order.Before(graph, call.call, edge.source, graph.blocks[edge.source.value].op_count), Error::kFlow);
          continue;
        }
        if (*owner == call.method || *owner == call.call) continue;
        if (result.owners[owner->value] == call.creation &&
            effects[owner->value] == call.creation)
          continue;  // Independently certified member installation, not an
                     // escape.
        const auto& op = graph.ops[owner->value];
        if (!self_stores.empty() && self_stores[owner->value]) {
          const auto& self = *self_stores[owner->value];
          require(self.creation == call.creation && self.initializer == call.call &&
              std::holds_alternative<ValueLexicalStore>(op.operation) && occurrence.role == OperandRole::kStoredValue,Error::kFlow);
          // The same creation's instance-initializer pass may already have
          // certified this store as one of its ordered effects.
          require(order.Before(graph, call.creation, op.block, op.order) &&
              order.Before(graph, *owner, invoke.block, invoke.order) &&
              (!effects[owner->value] || effects[owner->value] == call.creation), Error::kFlow);
          effects[owner->value] = call.creation;
          continue; // Retained cell write, never projected away.
        }
        // Every unowned observation, including a phi's actual incoming edge,
        // must occur after the call on all paths. Numeric placement says nothing
        // about an observer in a bypass arm or an earlier loop iteration.
        if (order.Before(graph, call.call, op.block, op.order)) continue;
        require(std::holds_alternative<ValueNamedRead>(op.operation) &&
            std::get<ValueNamedRead>(op.operation).lookup == PropertyLookup::kObject && op.use_count == 1 &&
            occurrence.role == OperandRole::kBase && op.result && unobserved(*op.result), Error::kFlow);
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
    return finish();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
