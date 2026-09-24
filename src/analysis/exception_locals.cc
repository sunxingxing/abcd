#include "abcd/analysis/exception_locals.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <tuple>

namespace abcd::analysis {
namespace {
auto Key(LocalCell cell) { return std::tuple(cell.environment.value, cell.slot.value); }
bool Captures(const SemanticOperation& operation) {
  return std::holds_alternative<FunctionCreation>(operation) ||
         std::holds_alternative<ClassCreation>(operation) ||
         std::holds_alternative<BufferedObjectCreation>(operation) ||
         std::holds_alternative<BufferedArrayCreation>(operation);
}
}
std::variant<ExceptionLocals, ExceptionLocalFailure> ProveExceptionLocals(
    const ExceptionFlow& flow, const EnvironmentFlow& environments, SemanticBudget budget,
    std::span<const CapturePreservation> preserved) {
  using Error = ExceptionLocalError;
  const auto fail = [](Error kind, abc::CodeOffset pc = {}) { return ExceptionLocalFailure{kind, pc}; };
  if (flow.nodes.empty() || flow.nodes.size() > UINT32_MAX ||
      flow.nodes.size() != environments.before.size()) return fail(Error::kInvalidEnvironment);
  try {
    ExceptionLocals result;
    auto& plan = result.plan;
    const auto charge = [&](std::uint64_t units = 1) {
      if (units > budget.work - plan.work_units) throw fail(Error::kResourceLimit);
      plan.work_units += units;
    };
    std::size_t spent = 0;
    const auto reserve = [&]<class T>(std::vector<T>& storage, std::size_t count) {
      if (count > (budget.bytes - spent) / sizeof(T)) throw fail(Error::kResourceLimit);
      storage.reserve(count);
      if (storage.capacity() > (budget.bytes - spent) / sizeof(T)) throw fail(Error::kResourceLimit);
      spent += storage.capacity() * sizeof(T);
    };
    // Validate complete same-flow PC coverage before accepting any exemption.
    // The byte flags avoid repeated searches at Before/Commit/Catch nodes.
    std::vector<std::uint8_t> read_only;
    if (!preserved.empty()) {
      reserve(read_only, flow.instructions.size());
      read_only.resize(flow.instructions.size());
      for (std::size_t p = 1; p < preserved.size(); ++p) {
        charge();
        if (preserved[p - 1].pc >= preserved[p].pc)
          return fail(Error::kInvalidEnvironment, preserved[p].pc);
      }
      std::size_t p = 0;
      for (std::size_t i = 0; i < flow.instructions.size(); ++i) {
        charge();
        const auto& instruction = flow.instructions[i];
        if (p == preserved.size() || preserved[p].pc > instruction.pc) continue;
        if (preserved[p].pc != instruction.pc || !Captures(instruction.operation))
          return fail(Error::kInvalidEnvironment, preserved[p].pc);
        read_only[i] = 1;
        ++p;
      }
      if (p != preserved.size()) return fail(Error::kInvalidEnvironment, preserved[p].pc);
    }
    reserve(plan.cells, environments.bindings.size());
    for (const auto& binding : environments.bindings) {
      charge();
      if (const auto* owner = std::get_if<EnvironmentId>(&binding.binding.environment))
        plan.cells.push_back({*owner, binding.binding.slot});
    }
    std::sort(plan.cells.begin(), plan.cells.end(), [&](auto a, auto b) { charge(); return Key(a) < Key(b); });
    plan.cells.erase(std::unique(plan.cells.begin(), plan.cells.end(),
        [&](auto a, auto b) { charge(); return Key(a) == Key(b); }), plan.cells.end());
    if (plan.cells.empty()) return result;
    const auto allocation_count = environments.environments.Size();
    std::vector<std::optional<ExceptionNodeId>> allocations, commits;
    std::vector<std::uint8_t> captured;
    std::vector<ExceptionCaptureScope> stores;
    NormalFlow graph;
    reserve(allocations, allocation_count);
    reserve(commits, flow.instructions.size());
    reserve(captured, allocation_count);
    reserve(stores, environments.bindings.size());
    reserve(plan.allocations, allocation_count);
    reserve(plan.accesses, environments.bindings.size());
    reserve(plan.stores, environments.bindings.size());
    reserve(result.loads, environments.bindings.size());
    reserve(graph.blocks, flow.nodes.size());
    allocations.resize(allocation_count);
    commits.resize(flow.instructions.size());
    captured.resize(allocation_count);
    plan.allocations.resize(allocation_count);
    for (const auto& allocation : environments.allocations) {
      charge();
      if (allocation.environment.value >= allocations.size() ||
          allocation.instruction >= flow.nodes.size()) return fail(Error::kInvalidEnvironment);
      const auto& node = flow.nodes[allocation.instruction];
      if (node.phase != ExceptionPhase::kCommit || allocations[allocation.environment.value])
        return fail(Error::kInvalidEnvironment);
      allocations[allocation.environment.value] = ExceptionNodeId{allocation.instruction};
      plan.allocations[allocation.environment.value] = node.instruction;
    }
    bool forward = true;
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      charge();
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= commits.size() || node.successor_count > node.successors.size())
        return fail(Error::kInvalidEnvironment);
      const auto& instruction = flow.instructions[node.instruction.value];
      if (node.phase == ExceptionPhase::kCommit) {
        if (commits[node.instruction.value]) return fail(Error::kInvalidEnvironment, instruction.pc);
        commits[node.instruction.value] = ExceptionNodeId{static_cast<std::uint32_t>(i)};
      }
      // v7 RuntimeCreateObjectHavingMethod clones literal methods with the
      // current environment. Without a pure-data literal proof, treat the
      // buffered-object/array operations as capture-capable too.
      const bool captures = Captures(instruction.operation) &&
          (read_only.empty() || !read_only[node.instruction.value]);
      if (node.phase == ExceptionPhase::kBefore && environments.before[i] && captures) {
        if (const auto* local = std::get_if<EnvironmentId>(&*environments.before[i])) {
          if (local->value >= captured.size()) return fail(Error::kInvalidEnvironment, instruction.pc);
          captured[local->value] = 1;
        }
      }
      FlowBlock block;
      block.begin = instruction.pc;
      block.end = instruction.pc;
      block.first_instruction = static_cast<std::uint32_t>(i);
      block.instruction_count = 1;
      block.successor_count = node.successor_count;
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto target = node.successors[e].target;
        if (target.value >= flow.nodes.size()) return fail(Error::kInvalidEnvironment, instruction.pc);
        const auto next = flow.nodes[target.value].instruction;
        if (next < node.instruction ||
            (next == node.instruction &&
             (node.phase == ExceptionPhase::kCommit ||
              node.successors[e].kind == ExceptionArcKind::kException)))
          forward = false;
        block.successors[e] = {BlockId{target.value}, FlowEdgeKind::kTaken};
      }
      graph.blocks.push_back(block);
    }
    // Environment IDs are parent-before-child. One reverse pass propagates
    // possible capture; never walk every ancestor for every creation or cell.
    for (std::size_t i = captured.size(); i > 0; --i) {
      charge();
      const auto* definition = environments.environments.Find(EnvironmentId{static_cast<std::uint32_t>(i - 1)});
      if (!definition || !allocations[i - 1]) return fail(Error::kInvalidEnvironment);
      if (const auto* parent = std::get_if<EnvironmentId>(&definition->parent)) {
        if (parent->value >= i - 1) return fail(Error::kInvalidEnvironment);
        captured[parent->value] |= captured[i - 1];
      }
    }
    const auto built = BuildNormalDominators(graph, {}, {budget.bytes - spent});
    const auto* dominance = std::get_if<DominatorTree>(&built);
    if (!dominance) return fail(std::get<DominatorError>(built) == DominatorError::kResourceLimit
        ? Error::kResourceLimit : Error::kInvalidEnvironment);
    charge(dominance->work.edge_visits);
    charge(dominance->work.eval_steps);
    const auto cell_id = [&](const BoundLexicalSite& binding) {
      const auto owner = std::get<EnvironmentId>(binding.binding.environment);
      const LocalCell key{owner, binding.binding.slot};
      const auto found = std::lower_bound(plan.cells.begin(), plan.cells.end(), key,
          [&](auto a, auto b) { charge(); return Key(a) < Key(b); });
      if (found == plan.cells.end() || Key(*found) != Key(key)) throw fail(Error::kInvalidEnvironment);
      return LocalCellId{static_cast<std::uint32_t>(found - plan.cells.begin())};
    };
    for (const auto& binding : environments.bindings) {
      charge();
      if (!std::holds_alternative<EnvironmentId>(binding.binding.environment)) continue;
      if (binding.instruction >= flow.nodes.size()) return fail(Error::kInvalidEnvironment);
      const auto& node = flow.nodes[binding.instruction];
      const auto& instruction = flow.instructions[node.instruction.value];
      if (node.phase != ExceptionPhase::kBefore || !commits[node.instruction.value])
        return fail(Error::kInvalidEnvironment, instruction.pc);
      const auto cell = cell_id(binding);
      const auto owner = plan.cells[cell.value].environment.value;
      if (owner >= allocations.size()) return fail(Error::kInvalidEnvironment, instruction.pc);
      const bool store = std::holds_alternative<LexicalStore>(instruction.operation);
      if (!store && !std::holds_alternative<LexicalLoad>(instruction.operation))
        return fail(Error::kInvalidEnvironment, instruction.pc);
      plan.accesses.push_back({node.instruction, cell, store ? LocalAccessKind::kStore : LocalAccessKind::kLoad});
      if (!store) continue;
      plan.stores.push_back({cell, node.instruction});
      const auto commit = commits[node.instruction.value]->value;
      if (dominance->Dominates(BlockId{allocations[owner]->value}, BlockId{commit})) {
        const auto& scope = dominance->nodes[commit];
        if (scope.begin) stores.push_back({cell, scope.begin, scope.end});
      }
    }
    std::sort(stores.begin(), stores.end(), [&](auto a, auto b) {
      charge();
      return std::tie(a.cell.value, a.begin) < std::tie(b.cell.value, b.begin);
    });
    // Per-cell dominance subtrees are nested/disjoint. Keep their union so
    // each load needs one binary search, not a scan of all preceding stores.
    std::size_t retained = 0;
    for (const auto scope : stores) {
      charge();
      if (retained && stores[retained - 1].cell.value == scope.cell.value &&
          scope.begin <= stores[retained - 1].end)
        stores[retained - 1].end = std::max(stores[retained - 1].end, scope.end);
      else stores[retained++] = scope;
    }
    stores.resize(retained);
    for (const auto& binding : environments.bindings) {
      charge();
      if (!std::holds_alternative<EnvironmentId>(binding.binding.environment)) continue;
      const auto& node = flow.nodes[binding.instruction];
      const auto& instruction = flow.instructions[node.instruction.value];
      if (!std::holds_alternative<LexicalLoad>(instruction.operation)) continue;
      const auto cell = cell_id(binding);
      if (captured[plan.cells[cell.value].environment.value]) return fail(Error::kCapturedEnvironment, instruction.pc);
      const auto point = dominance->nodes[binding.instruction].begin;
      const auto key = std::tuple(cell.value, point);
      const auto found = std::upper_bound(stores.begin(), stores.end(), key,
          [&](auto a, const auto& b) { charge(); return a < std::tuple(b.cell.value, b.begin); });
      if (!point || found == stores.begin() || (found - 1)->cell.value != cell.value ||
          point >= (found - 1)->end) return fail(Error::kUninitialized, instruction.pc);
      result.loads.push_back({*commits[node.instruction.value]});
    }
    std::sort(plan.stores.begin(), plan.stores.end(), [&](auto a, auto b) {
      charge();
      return std::tie(a.cell.value, a.instruction.value) < std::tie(b.cell.value, b.instruction.value);
    });
    if (forward) {
      // The tree is still live: include its result in admission for the
      // retained phase-entry index. Move scopes rather than copy their buffer.
      if (dominance->work.allocated_bytes > budget.bytes - spent)
        return fail(Error::kResourceLimit);
      spent += dominance->work.allocated_bytes;
      reserve(result.capture_entries, flow.nodes.size());
      for (const auto& node : dominance->nodes) {
        charge();
        result.capture_entries.push_back(node.begin);
      }
      std::size_t scopes = 0;
      for (const auto scope : stores) {
        charge();
        if (!captured[plan.cells[scope.cell.value].environment.value])
          stores[scopes++] = scope;
      }
      stores.resize(scopes);
      result.capture_scopes = std::move(stores);
    }
    return result;
  } catch (const ExceptionLocalFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return fail(Error::kResourceLimit);
  } catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
}  // namespace abcd::analysis
