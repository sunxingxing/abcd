#include "cyclic_local_initialization.h"
#include <algorithm>
#include <bit>
#include <tuple>
namespace abcd::analysis {
std::variant<std::uint64_t, SemanticProofFailure> ProveCyclicLocalInitialization(
    const SemanticMethod& method, LocalAccessPlan& plan,
    std::span<const std::optional<InstructionId>> allocations,
    SemanticBudget budget, ClosureWrites writes) {
  const auto initial = budget;
  const auto resource = [] { return SemanticProofFailure{SemanticProofError::kResourceLimit, {}}; };
  const auto step = [&](std::uint64_t n = 1) {
    if (n > budget.work) throw resource();
    budget.work -= n;
  };
  const auto reserve = [&](auto& items, std::size_t n) {
    using T = typename std::decay_t<decltype(items)>::value_type;
    if (n > budget.bytes / sizeof(T)) throw resource();
    items.reserve(n);
    if (items.capacity() > budget.bytes / sizeof(T)) throw resource();
    budget.bytes -= items.capacity() * sizeof(T);
  };
  auto built = BuildNormalDominators(method.flow, {}, {budget.bytes, budget.work});
  auto* dominance = std::get_if<DominatorTree>(&built);
  if (!dominance || dominance->work.allocated_bytes > budget.bytes) return resource();
  budget.bytes -= dominance->work.allocated_bytes;
  step(dominance->work.steps);
  reserve(plan.block_entries, method.flow.blocks.size());
  reserve(plan.block_exits, method.flow.blocks.size());
  for (const auto& node : dominance->nodes) {
    step(); plan.block_entries.push_back(node.begin); plan.block_exits.push_back(node.end);
  }
  reserve(plan.initializations, plan.stores.size());
  const auto block = [&](InstructionId site) {
    step(std::bit_width(method.flow.blocks.size()) + 1);
    const auto after = std::upper_bound(method.flow.blocks.begin(), method.flow.blocks.end(), site.value,
        [](auto i, const auto& b) { return i < b.first_instruction; });
    if (after == method.flow.blocks.begin()) throw resource();
    return BlockId{static_cast<std::uint32_t>(after - method.flow.blocks.begin() - 1)};
  };
  for (const auto& store : plan.stores) {
    step(); const auto allocation = allocations[plan.cells[store.cell.value].environment.value];
    if (!allocation) continue;
    const auto a = block(*allocation), s = block(store.instruction);
    const auto& scope = dominance->nodes[s.value];
    if (scope.begin && dominance->Dominates(a, s) && (a != s || *allocation < store.instruction))
      plan.initializations.push_back({store.cell, store.instruction, s, scope.begin, scope.end});
  }
  std::sort(plan.initializations.begin(), plan.initializations.end(), [&](const auto& a, const auto& b) {
    step(); return std::tuple(a.cell.value, a.begin, a.store.value) < std::tuple(b.cell.value, b.begin, b.store.value);
  });
  std::size_t kept = 0;
  for (const auto fact : plan.initializations) {
    step();
    if (kept && plan.initializations[kept-1].cell.value == fact.cell.value && fact.begin < plan.initializations[kept-1].end) continue;
    plan.initializations[kept++] = fact;
  }
  plan.initializations.resize(kept);
  for (const auto& access : plan.accesses) {
    step(); if (access.kind != LocalAccessKind::kLoad) continue;
    const auto allocation = allocations[plan.cells[access.cell.value].environment.value];
    step(std::bit_width(plan.initializations.size()) + 1);
    const auto store = plan.InitializationBefore(access.cell, block(access.instruction), access.instruction);
    // This increment does not solve unknown closure writes around cycles.
    // With no unpreserved creation there is no such writer. Otherwise require
    // the existing family-wide Language premise; never infer it from a loop.
    const bool preserved = writes == ClosureWrites::kLanguage || plan.capture_barriers.empty();
    if (!allocation || !store || !preserved)
      return SemanticProofFailure{SemanticProofError::kInternalValue, method.instructions[access.instruction.value].pc,
          0, 0, InternalValueEvidence{!allocation || !store ? InternalValueReason::kCellInitialization :
              InternalValueReason::kCellPreservation, {}, access.instruction, {}, {}, {},
              plan.cells[access.cell.value], allocation, store}};
  }
  return initial.work - budget.work;
}
}
