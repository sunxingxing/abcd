#include "semantic_locals.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <tuple>

namespace abcd::analysis {
namespace {
auto Key(LocalCell cell) {
  return std::tuple(cell.environment.value, cell.slot.value);
}
}  // namespace
std::variant<LocalAccessPlan, SemanticProofFailure> ProveLocalAccesses(
    const SemanticMethod& method, const EnvironmentFlow& environments,
    SemanticBudget budget, ClosureWrites closure_writes) {
  const auto resource = [] {
    return SemanticProofFailure{SemanticProofError::kResourceLimit, {}};
  };
  try {
    auto remaining_work = budget.work;
    const auto charge = [&] {
      if (!remaining_work) throw resource();
      --remaining_work;
    };
    LocalAccessPlan plan;
    const auto count = environments.bindings.size();
    constexpr auto kCellBytes = sizeof(LocalCell) + sizeof(LocalAccessFact) +
                               sizeof(LocalStoreSite) +
                               sizeof(std::optional<InstructionId>);
    if (count > budget.bytes / kCellBytes) return resource();
    plan.cells.reserve(count);
    plan.accesses.reserve(count);
    plan.stores.reserve(count);
    for (const auto& site : environments.bindings) {
      charge();
      if (const auto* owner = std::get_if<EnvironmentId>(&site.binding.environment))
        plan.cells.push_back({*owner, site.binding.slot});
    }
    std::sort(plan.cells.begin(), plan.cells.end(), [&](auto a, auto b) {
      charge();
      return Key(a) < Key(b);
    });
    plan.cells.erase(std::unique(plan.cells.begin(), plan.cells.end(),
        [&](auto a, auto b) { charge(); return Key(a) == Key(b); }), plan.cells.end());
    if (plan.AllocatedBytes() > budget.bytes) return resource();
    auto remaining = budget.bytes - plan.AllocatedBytes();
    if (plan.cells.size() > remaining / sizeof(std::optional<InstructionId>))
      return resource();
    std::vector<std::optional<InstructionId>> stores(plan.cells.size());
    if (stores.capacity() > remaining / sizeof(std::optional<InstructionId>))
      return resource();
    remaining -= stores.capacity() * sizeof(std::optional<InstructionId>);
    if (environments.environments.Size() >
        remaining / sizeof(std::optional<InstructionId>)) return resource();
    std::vector<std::optional<InstructionId>> allocations(environments.environments.Size());
    if (allocations.capacity() > remaining / sizeof(std::optional<InstructionId>))
      return resource();
    for (const auto& allocation : environments.allocations) {
      charge();
      if (allocation.environment.value >= allocations.size()) return resource();
      allocations[allocation.environment.value] = InstructionId{allocation.instruction};
    }
    remaining -= allocations.capacity() * sizeof(std::optional<InstructionId>);
    bool forward = true;
    for (std::size_t i = 0; i < method.flow.blocks.size(); ++i)
      for (std::size_t edge = 0; edge < method.flow.blocks[i].successor_count; ++edge) {
        charge();
        if (method.flow.blocks[i].successors[edge].target.value <= i) forward = false;
      }
    std::optional<DominatorTree> dominance;
    if (forward && !plan.cells.empty()) {
      if (plan.cells.size() > remaining / sizeof(std::optional<LocalInitialization>))
        return resource();
      plan.initializations.resize(plan.cells.size());
      const auto init_bytes = plan.initializations.capacity() *
                              sizeof(std::optional<LocalInitialization>);
      if (init_bytes > remaining) return resource();
      remaining -= init_bytes;
      if (method.flow.blocks.size() > remaining / sizeof(std::uint32_t)) return resource();
      plan.block_entries.resize(method.flow.blocks.size());
      const auto entry_bytes = plan.block_entries.capacity() * sizeof(std::uint32_t);
      if (entry_bytes > remaining) return resource();
      remaining -= entry_bytes;
      auto built = BuildNormalDominators(method.flow, {}, {remaining});
      auto* tree = std::get_if<DominatorTree>(&built);
      if (!tree) return resource();
      if (tree->work.edge_visits > remaining_work) return resource();
      remaining_work -= tree->work.edge_visits;
      if (tree->work.eval_steps > remaining_work) return resource();
      remaining_work -= tree->work.eval_steps;
      dominance.emplace(std::move(*tree));
      for (std::size_t i = 0; i < plan.block_entries.size(); ++i)
        plan.block_entries[i] = dominance->nodes[i].begin;
    }
    std::size_t block = 0;
    std::size_t binding = 0;
    std::optional<InstructionId> creation;
    for (std::size_t i = 0; i < method.instructions.size(); ++i) {
      charge();
      while (block + 1 < method.flow.blocks.size() &&
             method.flow.blocks[block + 1].first_instruction <= i) ++block;
      const auto first = method.flow.blocks[block].first_instruction;
      const auto& operation = method.instructions[i].operation;
      if (std::holds_alternative<FunctionCreation>(operation) ||
          std::holds_alternative<ClassCreation>(operation))
        creation = InstructionId{static_cast<std::uint32_t>(i)};
      if (binding >= environments.bindings.size() ||
          environments.bindings[binding].instruction != i) continue;
      const auto& site = environments.bindings[binding++];
      const auto* owner = std::get_if<EnvironmentId>(&site.binding.environment);
      if (!owner) continue;
      const LocalCell cell{*owner, site.binding.slot};
      const auto found = std::lower_bound(plan.cells.begin(), plan.cells.end(), cell,
          [&](auto a, auto b) { charge(); return Key(a) < Key(b); });
      if (found == plan.cells.end() || Key(*found) != Key(cell)) return resource();
      const auto id = static_cast<std::uint32_t>(found - plan.cells.begin());
      const InstructionId instruction{static_cast<std::uint32_t>(i)};
      if (std::holds_alternative<LexicalStore>(operation)) {
        stores[id] = instruction;
        plan.accesses.push_back({instruction, {id}, LocalAccessKind::kStore});
        plan.stores.push_back({{id}, instruction});
        const auto allocated = allocations[owner->value];
        if (dominance && !plan.initializations[id] && allocated &&
            allocated->value < i) {
          // In a forward CFG, the same environment allocation may precede a
          // branch and initialize only after its join. Require dominance of
          // the store, not textual co-location. Store dominance is retained
          // below for each later read; loops still use the strict local path.
          const auto after_allocation = std::upper_bound(
              method.flow.blocks.begin(), method.flow.blocks.end(),
              allocated->value, [&](auto instruction, const FlowBlock& region) {
                charge();
                return instruction < region.first_instruction;
              });
          if (after_allocation == method.flow.blocks.begin())
            return SemanticProofFailure{SemanticProofError::kEnvironment,
                                         method.instructions[i].pc};
          const auto allocation_block = static_cast<std::uint32_t>(
              after_allocation - method.flow.blocks.begin() - 1);
          const auto& scope = dominance->nodes[block];
          if (scope.begin && dominance->Dominates(
                  BlockId{allocation_block},
                  BlockId{static_cast<std::uint32_t>(block)}))
            plan.initializations[id] = LocalInitialization{
                instruction, scope.begin, scope.end};
        }
      } else if (std::holds_alternative<LexicalLoad>(operation)) {
        const auto allocated = allocations[owner->value];
        const bool same_block = allocated && allocated->value >= first &&
                                stores[id] && stores[id]->value >= first;
        bool initialized = same_block;
        if (!initialized && dominance && plan.initializations[id]) {
          const auto& scope = *plan.initializations[id];
          const auto entry = plan.block_entries[block];
          initialized = scope.store.value < i &&
                        scope.begin <= entry && entry < scope.end;
        }
        if (!allocated || allocated->value >= i || !initialized ||
            (closure_writes == ClosureWrites::kUnknown && creation &&
             creation->value >= allocated->value))
          return SemanticProofFailure{SemanticProofError::kInternalValue,
                                       method.instructions[i].pc};
        plan.accesses.push_back({instruction, {id}, LocalAccessKind::kLoad});
      } else {
        return SemanticProofFailure{SemanticProofError::kEnvironment,
                                     method.instructions[i].pc};
      }
    }
    std::sort(plan.stores.begin(), plan.stores.end(), [&](auto a, auto b) {
      charge();
      return std::tie(a.cell.value, a.instruction.value) <
             std::tie(b.cell.value, b.instruction.value);
    });
    plan.allocations = std::move(allocations);
    plan.work_units = budget.work - remaining_work;
    return plan;
  } catch (const SemanticProofFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return resource();
  } catch (const std::length_error&) {
    return resource();
  }
}
}  // namespace abcd::analysis
