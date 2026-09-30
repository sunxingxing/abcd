#include "class_accessors.h"
#include "cell_evaluation.h"
#include "../analysis/value_edits.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<ClassAccessorProjection, ClassAccessorError> ProjectClassAccessors(
    const analysis::ValueProgram& graph, std::span<const ClassAccessorSite> requested,
    analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ClassAccessorError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error = Error::kInput) { if (!ok) throw error; };
    const auto step = [&](std::size_t n = 1) {
      require(n <= budget.work, Error::kWorkLimit); budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      require(n <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= n * width;
    };
    ClassAccessorProjection result;
    if (requested.empty()) return result;
    auto discovered = FindClassAccessors(graph, budget);
    if (const auto* error = std::get_if<Error>(&discovered)) return *error;
    const auto& candidates = std::get<ClassAccessorSites>(discovered);
    admit(candidates.allocated_bytes, 1); step(candidates.work);
    // Dense operation indexes and prefix sums make the class use census once
    // per class, not once per accessor. Shared prototype reads are claimed once.
    struct Corridor { std::optional<OpId> last; std::size_t effects = 0; };
    admit(graph.ops.size(), sizeof(Corridor));
    std::vector<Corridor> corridors(graph.ops.size());
    admit(corridors.capacity() - corridors.size(), sizeof(Corridor));
    admit(graph.ops.size() + 1, sizeof(std::size_t));
    std::vector<std::size_t> effects(graph.ops.size() + 1);
    admit(effects.capacity() - effects.size(), sizeof(std::size_t));
    admit(graph.ops.size(), sizeof(std::optional<OpId>));
    result.owners.resize(graph.ops.size());
    admit(result.owners.capacity() - result.owners.size(), sizeof(std::optional<OpId>));
    admit(requested.size(), sizeof(OpId)); result.prototypes.reserve(requested.size());
    admit(result.prototypes.capacity() - requested.size(), sizeof(OpId));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      require(op.block.value < graph.blocks.size());
      const auto& block = graph.blocks[op.block.value];
      require(i >= block.first_op.value && i - block.first_op.value < block.op_count &&
          op.order == i - block.first_op.value);
      effects[i + 1] = effects[i] + (op.life == ValueLife::kLive &&
          (op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
           op.exceptional != ExceptionalCompletion::kImpossible || IsEnvironmentState(op.operation) ||
           std::holds_alternative<ValueDebugger>(op.operation)));
    }
    std::size_t next = 0;
    std::optional<OpId> previous;
    for (const auto& site : requested) {
      step(); require(!previous || *previous < site.definition); previous = site.definition;
      while (next < candidates.sites.size() && candidates.sites[next].definition < site.definition) { step(); ++next; }
      require(next < candidates.sites.size() && candidates.sites[next] == site); ++next;
      auto& corridor = corridors[site.creation.value];
      corridor.last = site.definition;
      const auto claim = [&](OpId id, bool shared = false) {
        require(id > site.creation && id <= site.definition && graph.ops[id.value].block == graph.ops[site.creation.value].block,
            Error::kFlow);
        if (result.owners[id.value]) {
          require(shared && result.owners[id.value] == site.creation); return;
        }
        result.owners[id.value] = site.creation;
        corridor.effects += effects[id.value + 1] - effects[id.value];
        if (shared) result.prototypes.push_back(id);
      };
      claim(site.definition);
      if (site.getter) claim(*site.getter);
      if (site.setter) claim(*site.setter);
      if (site.prototype) claim(*site.prototype, true);
      // The VM returns the home object. Until source alias correspondence is
      // established, even a later use must not disappear with the definition.
      require(!graph.values[graph.ops[site.definition.value].result->value].first_use, Error::kFlow);
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& corridor = corridors[i];
      if (!corridor.last) continue;
      const auto& creation = graph.ops[i];
      const auto& end = graph.ops[corridor.last->value];
      require(effects[corridor.last->value + 1] - effects[i + 1] == corridor.effects, Error::kFlow);
      require(creation.result.has_value());
      for (auto use = graph.values[creation.result->value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto* owner = std::get_if<OpId>(&graph.uses[use->value].owner);
        require(owner && owner->value < graph.ops.size(), Error::kFlow);
        if (result.owners[owner->value] == OpId{static_cast<std::uint32_t>(i)}) continue;
        const auto& observer = graph.ops[owner->value];
        // Other blocks need a separate dominance/lifetime proof. Do not infer
        // that a numerically later block observes the fully defined class.
        require(observer.block == end.block && observer.order > end.order, Error::kFlow);
      }
    }
    for (const auto prototype : result.prototypes) {
      step(); const auto& op = graph.ops[prototype.value]; require(op.result.has_value());
      const auto creation = result.owners[prototype.value]; require(creation.has_value());
      const auto& end = graph.ops[corridors[creation->value].last->value];
      bool retain = false;
      for (auto use = graph.values[op.result->value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto* owner = std::get_if<OpId>(&graph.uses[use->value].owner);
        require(owner && owner->value < graph.ops.size(), Error::kFlow);
        if (result.owners[owner->value] == creation) continue;
        const auto& observer = graph.ops[owner->value];
        require(observer.block == end.block && observer.order > end.order, Error::kFlow);
        retain = true;
      }
      // A later ordinary method may share this prototype. Keep its actual read
      // and SSA identity rather than erase a live value or duplicate the class.
      // The adapter still proves the fresh class's builtin prototype property.
      if (retain) result.owners[prototype.value].reset();
    }
    // Literal key/name-flag leaves with no remaining consumer belong to the
    // same projection. Keep shared/external leaves; never erase an evaluation
    // merely because the accessor itself no longer has a source statement.
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
