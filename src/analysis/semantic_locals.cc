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
    SemanticBudget budget) {
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
    std::size_t block = 0;
    std::size_t binding = 0;
    std::optional<InstructionId> creation;
    for (std::size_t i = 0; i < method.instructions.size(); ++i) {
      charge();
      while (block + 1 < method.flow.blocks.size() &&
             method.flow.blocks[block + 1].first_instruction <= i) ++block;
      const auto first = method.flow.blocks[block].first_instruction;
      const auto& operation = method.instructions[i].operation;
      if (std::holds_alternative<FunctionCreation>(operation))
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
      } else if (std::holds_alternative<LexicalLoad>(operation)) {
        const auto allocated = allocations[owner->value];
        if (!allocated || allocated->value < first || allocated->value >= i ||
            !stores[id] || stores[id]->value < first ||
            (creation && creation->value >= allocated->value))
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
