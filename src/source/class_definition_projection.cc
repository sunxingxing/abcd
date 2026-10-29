#include <algorithm>
#include <functional>
#include <new>
#include <stdexcept>

#include "../analysis/value_edits.h"
#include "cell_evaluation.h"
#include "class_definitions.h"
#include "class_self_order.h"
#include "effect_order.h"
namespace abcd::source {
std::variant<ClassDefinitionProjection, ClassDefinitionError>
ProjectClassDefinitions(const analysis::ValueProgram& graph,
                        std::span<const ClassDefinitionSite> requested,
                        analysis::SemanticBudget budget,
                        const ClassSelfOrder* self_order,
                        std::span<const analysis::OpId> home_consumers) {
  using namespace analysis;
  using Error = ClassDefinitionError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error = Error::kInput) { if (!ok) throw error; };
    const auto step = [&](std::size_t n = 1) {
      require(n <= budget.work, Error::kWorkLimit); budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      require(n <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= n * width;
    };
    ClassDefinitionProjection result;
    if (requested.empty()) return result;
    auto discovered = FindClassDefinitions(graph, budget);
    if (const auto* error = std::get_if<Error>(&discovered)) return *error;
    const auto& candidates = std::get<ClassDefinitionSites>(discovered);
    admit(candidates.allocated_bytes, 1); step(candidates.work);
    auto built = BuildEffectOrder(graph, budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&built)) {
      if (*error == ValueAvailabilityError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    const auto& order = std::get<EffectOrder>(built);
    admit(order.allocated_bytes, 1); step(order.work);
    // Private class effects must form one identity-preserving predecessor
    // chain rooted at creation. Pure splits/joins/loops are transparent;
    // branch-skipped installs and intervening effects cannot join this chain.
    // Regression: class_initializers_test, class_accessor_tree_source_test.
    struct Corridor { std::optional<OpId> last; std::optional<OpId> tail; };
    admit(graph.ops.size(), sizeof(Corridor));
    std::vector<Corridor> corridors(graph.ops.size());
    admit(corridors.capacity() - corridors.size(), sizeof(Corridor));
    admit(graph.ops.size(), sizeof(std::uint8_t));
    std::vector<std::uint8_t> successors(graph.ops.size());
    admit(successors.capacity() - successors.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::optional<OpId>));
    result.owners.resize(graph.ops.size());
    admit(result.owners.capacity() - result.owners.size(), sizeof(std::optional<OpId>));
    admit(requested.size(), sizeof(OpId)); result.prototypes.reserve(requested.size());
    admit(result.prototypes.capacity() - requested.size(), sizeof(OpId));
    std::size_t next = 0;
    std::optional<OpId> previous;
    for (const auto& site : requested) {
      step(); require(!previous || *previous < site.definition); previous = site.definition;
      while (next < candidates.sites.size() && candidates.sites[next].definition < site.definition) { step(); ++next; }
      require(next < candidates.sites.size() && candidates.sites[next] == site); ++next;
      auto& corridor = corridors[site.creation.value];
      if (!corridor.last || order.position[corridor.last->value] < site.order.value) corridor.last = site.definition;
      const auto claim = [&](OpId id, bool shared = false) {
        const auto& op = graph.ops[id.value]; const auto& definition = graph.ops[site.definition.value];
        require(order.Before(graph, site.creation, op.block, op.order) &&
            (id == site.definition || order.Before(graph, id, definition.block, definition.order)), Error::kFlow);
        if (result.owners[id.value]) {
          require(shared && result.owners[id.value] == site.creation); return;
        }
        result.owners[id.value] = site.creation;
        if (shared) result.prototypes.push_back(id);
      };
      claim(site.definition);
      for (const auto& method : site.Methods()) if (method.operation) claim(*method.operation);
      if (site.prototype) claim(*site.prototype, true);
      // An accessor definition returns its home object. Until alias correspondence is
      // established, even a later use must not disappear with the definition.
      // The single exception: the home operand of a stored instance initializer
      // (sorted home_consumers), which the caller then projects into this class.
      const auto value = graph.ops[site.definition.value].result;
      if (value) for (auto use = graph.values[value->value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value];
        const auto* consumer = std::get_if<OpId>(&occurrence.owner);
        require(consumer && occurrence.role == OperandRole::kHomeObject &&
            std::binary_search(home_consumers.begin(), home_consumers.end(), *consumer), Error::kFlow);
      }
    }
    std::vector<std::optional<OpId>> private_stores;
    if (self_order && !self_order->Installations(graph).empty()) {
      admit(graph.ops.size(), sizeof(std::optional<OpId>));
      private_stores.resize(graph.ops.size());
      admit(private_stores.capacity() - private_stores.size(),
            sizeof(std::optional<OpId>));
      for (const auto& store : self_order->Stores(graph)) {
        step();
        require(store.creation.value < corridors.size() &&
                store.store.value < graph.ops.size());
        const auto end = corridors[store.creation.value].last;
        if (end && order.Before(graph, store.store, graph.ops[end->value].block,
                                graph.ops[end->value].order))
          private_stores[store.store.value] = store.creation;
      }
    }
    const auto owner = [&](std::size_t i) {
      return result.owners[i]         ? result.owners[i]
             : private_stores.empty() ? std::optional<OpId>{}
                                      : private_stores[i];
    };
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      if (!owner(i) || !HasOrderedEffect(graph.ops[i])) continue;
      const auto creation = *owner(i);
      const auto* predecessor = std::get_if<OpId>(&order.before[i]);
      require(predecessor && (*predecessor == creation ||
                              owner(predecessor->value) == creation),
              Error::kFlow);
      require(order.Before(graph, *predecessor, graph.ops[i].block, graph.ops[i].order), Error::kFlow);
      require(!successors[predecessor->value], Error::kFlow); successors[predecessor->value] = 1;
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      if (!owner(i) || !HasOrderedEffect(graph.ops[i]) || successors[i])
        continue;
      auto& corridor = corridors[owner(i)->value];
      require(!corridor.tail, Error::kFlow); corridor.tail = OpId{static_cast<std::uint32_t>(i)};
    }
    // Pure register copies (kept by phased graphs) are not observations:
    // their own uses are. They stay evaluation aliases of their sources.
    const auto pure_copy = [&](OpId id) {
      const auto& op = graph.ops[id.value];
      return !graph.phases.empty() && op.life == ValueLife::kLive && op.result &&
          std::holds_alternative<ValueCopy>(op.operation) && CanErasePureOp(op);
    };
    std::function<void(const OperandUse&, OpId, OpId)> completed;
    completed = [&](const OperandUse& use, OpId creation, OpId end) {
      WorkBlockId block; std::uint32_t position;
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        require(owner->value < graph.ops.size(), Error::kFlow);
        if (result.owners[owner->value] == creation ||
            (!private_stores.empty() &&
             private_stores[owner->value] == creation))
          return;
        if (pure_copy(*owner)) {
          for (auto next = graph.values[graph.ops[owner->value].result->value].first_use; next;
               next = graph.uses[next->value].next) {
            step(); completed(graph.uses[next->value], creation, end);
          }
          return;
        }
        const auto& observer = graph.ops[owner->value];
        block = observer.block; position = observer.order;
      } else {
        // A phi observes the value on its actual incoming edge, not at the
        // merge's numeric position. Availability includes parallel/back edges.
        require(use.incoming && std::holds_alternative<EdgeId>(*use.incoming), Error::kFlow);
        const auto& edge = graph.edges[std::get<EdgeId>(*use.incoming).value];
        block = edge.source; position = graph.blocks[block.value].op_count;
      }
      require(order.Before(graph, end, block, position), Error::kFlow);
    };
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& corridor = corridors[i];
      if (!corridor.last) continue;
      const auto& creation = graph.ops[i];
      require(corridor.tail == corridor.last && HasOrderedEffect(creation), Error::kFlow);
      require(creation.result.has_value());
      for (auto use = graph.values[creation.result->value].first_use; use; use = graph.uses[use->value].next) {
        step(); completed(graph.uses[use->value], OpId{static_cast<std::uint32_t>(i)}, *corridor.last);
      }
    }
    for (const auto prototype : result.prototypes) {
      step(); const auto& op = graph.ops[prototype.value]; require(op.result.has_value());
      const auto creation = result.owners[prototype.value]; require(creation.has_value());
      const auto end = *corridors[creation->value].last;
      bool retain = false;
      for (auto use = graph.values[op.result->value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value];
        if (const auto* owner = std::get_if<OpId>(&occurrence.owner); owner && result.owners[owner->value] == creation) continue;
        completed(occurrence, *creation, end);
        retain = true;
      }
      // A later ordinary method may share this prototype. Keep its actual read
      // and SSA identity rather than erase a live value or duplicate the class.
      // The adapter still proves the fresh class's builtin prototype property.
      if (retain) result.owners[prototype.value].reset();
    }
    // Literal key/name-flag leaves with no remaining consumer belong to the
    // same projection. Keep shared/external leaves; never erase an evaluation
    // merely because its member definition no longer has a source statement.
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !op.result || !CanErasePureOp(op)) continue;
      const bool literal_copy = std::holds_alternative<ValueCopy>(op.operation) && op.use_count == 1 &&
          std::holds_alternative<SpecialValue>(graph.uses[op.first_use.value].operand);
      if (!literal_copy && !std::holds_alternative<ValueString>(op.operation)) continue;
      std::optional<OpId> creation; bool exclusively_owned = true;
      for (auto use = graph.values[op.result->value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto* owner = std::get_if<OpId>(&graph.uses[use->value].owner);
        if (!owner || !result.owners[owner->value] || (creation && creation != result.owners[owner->value])) {
          exclusively_owned = false; break;
        }
        creation = result.owners[owner->value];
      }
      if (exclusively_owned && creation) result.owners[i] = creation;
    }
    result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
