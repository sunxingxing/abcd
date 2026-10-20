#include "semantic_locals.h"
#include "cyclic_local_initialization.h"
#include "abcd/analysis/block_order.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace abcd::analysis {
namespace {
auto Key(LocalCell cell) {
  return std::tuple(cell.environment.value, cell.slot.value);
}
bool CapturesEnvironment(const SemanticOperation& operation) {
  return std::holds_alternative<FunctionCreation>(operation) ||
         std::holds_alternative<ClassCreation>(operation) ||
         std::holds_alternative<BufferedObjectCreation>(operation) ||
         std::holds_alternative<BufferedArrayCreation>(operation);
}
}  // namespace
std::optional<InstructionId> LocalAccessPlan::InitializationBefore(
    LocalCellId cell, BlockId block, InstructionId site) const {
  if (block.value >= block_entries.size() || !block_entries[block.value]) return {};
  const auto entry = block_entries[block.value];
  auto found = std::upper_bound(initializations.begin(), initializations.end(), std::tuple(cell.value, entry),
      [](auto key, const LocalInitialization& fact) { return key < std::tuple(fact.cell.value, fact.begin); });
  if (found == initializations.begin()) return {};
  const auto& fact = *--found;
  if (fact.cell.value != cell.value || entry < fact.begin || entry >= fact.end ||
      (fact.block == block && fact.store >= site)) return {};
  return fact.store;
}
bool LocalAccessPlan::PreservesSince(InstructionId allocation, InstructionId site) const {
  if (!site_order.empty()) {
    if (allocation.value >= site_order.size() || site.value >= site_order.size() ||
        site.value >= capture_before.size() || !site_order[allocation.value] || !site_order[site.value] ||
        *site_order[allocation.value] >= *site_order[site.value]) return false;
    return !capture_before[site.value] || *capture_before[site.value] < *site_order[allocation.value];
  }
  // Existing cyclic path is admitted only for a fresh same-block allocation;
  // initialization must separately establish that activation-local ownership.
  const auto next = std::lower_bound(capture_barriers.begin(), capture_barriers.end(), site);
  return next == capture_barriers.begin() || *(next - 1) < allocation;
}
std::variant<LocalAccessPlan, SemanticProofFailure> ProveLocalAccesses(
    const SemanticMethod& method, const EnvironmentFlow& environments,
    SemanticBudget budget, ClosureWrites closure_writes,
    std::span<const CapturePreservation> preserved) {
  const auto resource = [] {
    return SemanticProofFailure{SemanticProofError::kResourceLimit, {}};
  };
  try {
    auto remaining_work = budget.work;
    const auto charge = [&] {
      if (!remaining_work) throw resource();
      --remaining_work;
    };
    for (std::size_t i = 1; i < preserved.size(); ++i) {
      charge();
      if (preserved[i - 1].pc >= preserved[i].pc)
        return SemanticProofFailure{SemanticProofError::kEnvironment,
                                     preserved[i].pc};
    }
    LocalAccessPlan plan;
    const auto count = environments.bindings.size();
    constexpr auto kCellBytes = sizeof(LocalCell) + sizeof(LocalAccessFact) +
                               sizeof(LocalStoreSite) +
                               sizeof(std::optional<InstructionId>);
    if (count > budget.bytes / kCellBytes) return resource();
    std::size_t captures_count = 0;
    for (const auto& instruction : method.instructions) {
      charge();
      captures_count += CapturesEnvironment(instruction.operation);
    }
    if (captures_count > (budget.bytes - count * kCellBytes) / sizeof(InstructionId))
      return resource();
    plan.capture_barriers.reserve(captures_count);
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
    using Unexposed = LocalAccessPlan::UnexposedInitialization;
    if (plan.cells.size() > remaining / sizeof(std::optional<Unexposed>)) return resource();
    plan.unexposed_initializations.resize(plan.cells.size());
    const auto unexposed_bytes = plan.unexposed_initializations.capacity() * sizeof(std::optional<Unexposed>);
    if (unexposed_bytes > remaining) return resource();
    remaining -= unexposed_bytes;
    if (plan.cells.size() > remaining / sizeof(std::optional<Unexposed>)) return resource();
    plan.module_prefix_initializations.resize(plan.cells.size());
    const auto module_prefix_bytes = plan.module_prefix_initializations.capacity() * sizeof(std::optional<Unexposed>);
    if (module_prefix_bytes > remaining) return resource();
    remaining -= module_prefix_bytes;
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
    // Physical backward edges need not repeat an environment activation. Use
    // the shared CFG order to distinguish actual cycles from block placement.
    // An unclosed cyclic read gets the separate activation proof below;
    // acyclic capture ranks are never manufactured for a cyclic graph.
    bool acyclic = false;
    std::optional<BlockOrder> order;
    if (!plan.cells.empty()) {
      // Ordering visits each edge at most three times, including failure on a
      // cycle. Charge that bound even when the API returns no partial product.
      for (const auto& region : method.flow.blocks) {
        charge(); charge(); charge();
        for (std::size_t edge = 0; edge < region.successor_count; ++edge) {
          charge(); charge(); charge();
        }
      }
      auto ordered = OrderAcyclicBlocks(method.flow, {}, {remaining});
      acyclic = std::holds_alternative<BlockOrder>(ordered);
      if (!acyclic && std::get<OrderError>(ordered) != OrderError::kCycle) return resource();
      if (acyclic) {
        order.emplace(std::get<BlockOrder>(std::move(ordered)));
        if (order->work.allocated_bytes > remaining) return resource();
        remaining -= order->work.allocated_bytes;
      }
    }
    std::optional<DominatorTree> dominance;
    if (acyclic && !plan.cells.empty()) {
      if (count > remaining / sizeof(LocalInitialization))
        return resource();
      plan.initializations.reserve(count);
      const auto init_bytes = plan.initializations.capacity() *
                              sizeof(LocalInitialization);
      if (init_bytes > remaining) return resource();
      remaining -= init_bytes;
      if (method.flow.blocks.size() > remaining / sizeof(std::uint32_t)) return resource();
      plan.block_entries.resize(method.flow.blocks.size());
      const auto entry_bytes = plan.block_entries.capacity() * sizeof(std::uint32_t);
      if (entry_bytes > remaining) return resource();
      remaining -= entry_bytes;
      if (method.flow.blocks.size() > remaining / sizeof(std::uint32_t)) return resource();
      plan.block_exits.resize(method.flow.blocks.size());
      const auto exit_bytes = plan.block_exits.capacity() * sizeof(std::uint32_t);
      if (exit_bytes > remaining) return resource();
      remaining -= exit_bytes;
      auto built = BuildNormalDominators(method.flow, {}, {remaining});
      auto* tree = std::get_if<DominatorTree>(&built);
      if (!tree) return resource();
      if (tree->work.edge_visits > remaining_work) return resource();
      remaining_work -= tree->work.edge_visits;
      if (tree->work.eval_steps > remaining_work) return resource();
      remaining_work -= tree->work.eval_steps;
      dominance.emplace(std::move(*tree));
      if (dominance->work.allocated_bytes > remaining) return resource();
      remaining -= dominance->work.allocated_bytes;
      for (std::size_t i = 0; i < plan.block_entries.size(); ++i) {
        charge();
        plan.block_entries[i] = dominance->nodes[i].begin;
        plan.block_exits[i] = dominance->nodes[i].end;
      }
    }
    std::optional<SemanticProofFailure> cyclic_gap;
    std::size_t block = 0;
    std::size_t binding = 0;
    std::optional<InstructionId> creation;
    std::size_t preservation = 0;
    std::optional<EnvironmentId> unexposed, module_prefix;
    bool published = false;
    for (std::size_t i = 0; i < method.instructions.size(); ++i) {
      charge();
      while (block + 1 < method.flow.blocks.size() &&
             method.flow.blocks[block + 1].first_instruction <= i) ++block;
      const auto first = method.flow.blocks[block].first_instruction;
      const auto& operation = method.instructions[i].operation;
      if (i == first) { unexposed.reset(); module_prefix.reset(); published = false; }
      // Only a fresh environment's uninterrupted, unobservable construction
      // prefix permits deferred capture initialization. Never restart after a
      // call, load, external store, branch or other possible observer.
      if (std::holds_alternative<PushEnvironment>(operation)) {
        unexposed.reset(); module_prefix.reset(); published = false;
        if (i + 1 < environments.before.size() && environments.before[i + 1]) {
          if (const auto* next = std::get_if<EnvironmentId>(&*environments.before[i + 1]);
              next && next->value < allocations.size() && allocations[next->value] &&
              allocations[next->value]->value == i) { unexposed = *next; module_prefix = *next; }
        }
      } else if (!std::holds_alternative<FunctionCreation>(operation) &&
          !std::holds_alternative<LexicalStore>(operation) &&
          !(std::holds_alternative<ScalarAssignment>(operation) &&
            method.instructions[i].effects.normal == LanguageEffect::kNone)) {
        unexposed.reset();
        if (std::holds_alternative<ModuleStore>(operation)) published = true;
        else { module_prefix.reset(); published = false; }
      }
      const bool captures = CapturesEnvironment(operation);
      bool read_only = false;
      if (preservation < preserved.size() &&
          preserved[preservation].pc <= method.instructions[i].pc) {
        charge();
        if (!captures || preserved[preservation].pc != method.instructions[i].pc)
          return SemanticProofFailure{SemanticProofError::kEnvironment,
                                       preserved[preservation].pc};
        read_only = true;
        ++preservation;
      }
      if (captures && !read_only) {
        creation = InstructionId{static_cast<std::uint32_t>(i)};
        plan.capture_barriers.push_back(*creation);
      }
      if (binding >= environments.bindings.size() ||
          environments.bindings[binding].instruction != i) continue;
      const auto& site = environments.bindings[binding++];
      const auto* owner = std::get_if<EnvironmentId>(&site.binding.environment);
      if (!owner || !unexposed || *owner != *unexposed) unexposed.reset();
      if (!owner || !module_prefix || *owner != *module_prefix) { module_prefix.reset(); published = false; }
      if (!owner) continue;
      const LocalCell cell{*owner, site.binding.slot};
      const auto found = std::lower_bound(plan.cells.begin(), plan.cells.end(), cell,
          [&](auto a, auto b) { charge(); return Key(a) < Key(b); });
      if (found == plan.cells.end() || Key(*found) != Key(cell)) return resource();
      const auto id = static_cast<std::uint32_t>(found - plan.cells.begin());
      const InstructionId instruction{static_cast<std::uint32_t>(i)};
      if (std::holds_alternative<LexicalStore>(operation)) {
        if (unexposed && !plan.unexposed_initializations[id])
          plan.unexposed_initializations[id] = Unexposed{*allocations[owner->value], instruction};
        if (module_prefix && published && !plan.module_prefix_initializations[id])
          plan.module_prefix_initializations[id] = Unexposed{*allocations[owner->value], instruction};
        stores[id] = instruction;
        plan.accesses.push_back({instruction, {id}, LocalAccessKind::kStore});
        plan.stores.push_back({{id}, instruction});
        const auto allocated = allocations[owner->value];
        if (dominance && allocated) {
          // In an acyclic CFG, the same environment allocation may precede a
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
          if (scope.begin && (allocation_block != block || allocated->value < i) && dominance->Dominates(
                  BlockId{allocation_block},
                  BlockId{static_cast<std::uint32_t>(block)}))
            plan.initializations.push_back(LocalInitialization{
                {id}, instruction, {static_cast<std::uint32_t>(block)}, scope.begin, scope.end});
        }
      } else if (std::holds_alternative<LexicalLoad>(operation)) {
        const auto allocated = allocations[owner->value];
        const bool same_block = allocated && allocated->value >= first &&
                                stores[id] && stores[id]->value >= first;
        // DAG reads are checked after all initialization regions are collected;
        // physical scanning cannot know a dominating store at a later address.
        if (!acyclic && !cyclic_gap && (!allocated || allocated->value >= i || !same_block ||
            (closure_writes == ClosureWrites::kUnknown && creation &&
             creation->value >= allocated->value)))
          cyclic_gap = SemanticProofFailure{SemanticProofError::kInternalValue,
                                       method.instructions[i].pc, 0, 0,
              InternalValueEvidence{InternalValueReason::kCyclicCellInitialization, {}, instruction, {}, {}, {},
                  cell, allocated, stores[id]}};
        plan.accesses.push_back({instruction, {id}, LocalAccessKind::kLoad});
      } else {
        return SemanticProofFailure{SemanticProofError::kEnvironment,
                                     method.instructions[i].pc};
      }
    }
    if (preservation != preserved.size())
      return SemanticProofFailure{SemanticProofError::kEnvironment,
                                   preserved[preservation].pc};
    // Existing successful plans retain their exact facts. Only an unclosed
    // cyclic local-read obligation requests the general activation proof.
    if (cyclic_gap) {
      auto checked = ProveCyclicLocalInitialization(method, plan, allocations,
          {remaining, remaining_work}, closure_writes);
      if (const auto* failure = std::get_if<SemanticProofFailure>(&checked)) return *failure;
      remaining_work -= std::get<std::uint64_t>(checked);
    } else if (!acyclic && !plan.cells.empty()) {
      // A loop with no unclosed local read still has capture queries. Index
      // completed stores as well as activation dominance: a child can capture
      // an initialized cell even when the creator never loads that cell.
      // No value is inferred from the backedge; allocation/store dominance and
      // the family-wide write contract remain separate obligations.
      auto built = BuildNormalDominators(method.flow, {}, {remaining, remaining_work});
      const auto* tree = std::get_if<DominatorTree>(&built);
      if (!tree || tree->work.allocated_bytes > remaining || tree->work.steps > remaining_work) return resource();
      remaining -= tree->work.allocated_bytes; remaining_work -= tree->work.steps;
      if (method.flow.blocks.size() > remaining / (2 * sizeof(std::uint32_t))) return resource();
      plan.block_entries.resize(method.flow.blocks.size());
      plan.block_exits.resize(method.flow.blocks.size());
      const auto bytes = (plan.block_entries.capacity() + plan.block_exits.capacity()) * sizeof(std::uint32_t);
      if (bytes > remaining) return resource();
      remaining -= bytes;
      for (std::size_t i = 0; i < tree->nodes.size(); ++i) {
        charge(); plan.block_entries[i] = tree->nodes[i].begin; plan.block_exits[i] = tree->nodes[i].end;
      }
      if (plan.stores.size() > remaining / sizeof(LocalInitialization)) return resource();
      plan.initializations.reserve(plan.stores.size());
      const auto initialization_bytes = plan.initializations.capacity() * sizeof(LocalInitialization);
      if (initialization_bytes > remaining) return resource();
      remaining -= initialization_bytes;
      const auto block_of = [&](InstructionId site) {
        const auto after = std::upper_bound(method.flow.blocks.begin(), method.flow.blocks.end(), site.value,
            [&](auto instruction, const FlowBlock& block) { charge(); return instruction < block.first_instruction; });
        if (after == method.flow.blocks.begin()) throw resource();
        return BlockId{static_cast<std::uint32_t>(after - method.flow.blocks.begin() - 1)};
      };
      for (const auto& store : plan.stores) {
        charge(); const auto allocation = allocations[plan.cells[store.cell.value].environment.value];
        if (!allocation) continue;
        const auto a = block_of(*allocation), b = block_of(store.instruction);
        const auto& scope = tree->nodes[b.value];
        if (scope.begin && tree->Dominates(a, b) && (a != b || *allocation < store.instruction))
          plan.initializations.push_back({store.cell, store.instruction, b, scope.begin, scope.end});
      }
    }
    if (acyclic || !plan.initializations.empty()) {
      std::sort(plan.initializations.begin(), plan.initializations.end(), [&](const auto& a, const auto& b) {
        charge(); return std::tuple(a.cell.value, a.begin, a.store.value) < std::tuple(b.cell.value, b.begin, b.store.value);
      });
      // Dominator regions nest or are disjoint. Keep each cell's outermost
      // regions; equal regions retain the earliest in-block store. No address
      // comparison chooses between different blocks or forwards a cell value.
      std::size_t kept = 0;
      for (const auto fact : plan.initializations) {
        charge();
        if (kept && plan.initializations[kept - 1].cell.value == fact.cell.value && fact.begin < plan.initializations[kept - 1].end) continue;
        plan.initializations[kept++] = fact;
      }
      plan.initializations.resize(kept);
    }
    if (acyclic) {
      const auto size = [&](auto& vector, std::size_t n) {
        using T = typename std::decay_t<decltype(vector)>::value_type;
        if (n > remaining / sizeof(T)) throw resource();
        vector.resize(n);
        if (vector.capacity() > remaining / sizeof(T)) throw resource();
        remaining -= vector.capacity() * sizeof(T);
      };
      size(plan.site_order, method.instructions.size()); size(plan.capture_before, method.instructions.size());
      std::vector<std::uint8_t> captures;
      std::vector<std::optional<InstructionRank>> incoming;
      size(captures, method.instructions.size()); size(incoming, method.flow.blocks.size());
      for (const auto site : plan.capture_barriers) { charge(); captures[site.value] = 1; }
      std::uint32_t rank = 0;
      for (const auto id : order->blocks) {
        charge(); const auto& region = method.flow.blocks[id.value]; auto latest = incoming[id.value];
        for (std::size_t i = region.first_instruction; i < std::size_t(region.first_instruction) + region.instruction_count; ++i) {
          charge(); const InstructionRank current{rank++};
          plan.site_order[i] = current; plan.capture_before[i] = latest;
          if (captures[i]) latest = current;
        }
        for (std::size_t e = 0; e < region.successor_count; ++e) {
          charge(); auto& target = incoming[region.successors[e].target.value];
          if (latest && (!target || *target < *latest)) target = latest;
        }
      }
      block = 0;
      for (const auto& access : plan.accesses) {
        charge(); if (access.kind != LocalAccessKind::kLoad) continue;
        const auto site = access.instruction;
        while (block + 1 < method.flow.blocks.size() && method.flow.blocks[block + 1].first_instruction <= site.value) { charge(); ++block; }
        const auto allocated = allocations[plan.cells[access.cell.value].environment.value];
        // Debit the indexed query's comparison bound; exposure itself is O(1).
        for (auto comparisons = std::bit_width(plan.initializations.size()); comparisons; --comparisons) charge();
        const auto initialized = plan.InitializationBefore(access.cell, {static_cast<std::uint32_t>(block)}, site);
        if (!allocated || !initialized ||
            (closure_writes == ClosureWrites::kUnknown && !plan.PreservesSince(*allocated, site)))
          return SemanticProofFailure{SemanticProofError::kInternalValue, method.instructions[site.value].pc, 0, 0,
              InternalValueEvidence{!allocated || !initialized ? InternalValueReason::kCellInitialization :
                  InternalValueReason::kCellPreservation, {}, site, {}, {}, {},
                  plan.cells[access.cell.value], allocated, initialized}};
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
