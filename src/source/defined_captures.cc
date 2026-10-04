#include "defined_captures.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <tuple>

#include "abcd/analysis/dominators.h"
#include "abcd/analysis/local_ssa.h"
#include "abcd/source/capture_preservation.h"

namespace abcd::source {
namespace {
bool GuardedLoad(const analysis::SemanticMethod& method,
                 const analysis::LocalAccessFact& access) {
  const auto i = access.instruction.value;
  if (access.kind != analysis::LocalAccessKind::kLoad ||
      i >= method.instructions.size() || i + 1 >= method.instructions.size())
    return false;
  const auto* check = std::get_if<analysis::SuperCheck>(
      &method.instructions[i + 1].operation);
  if (!check || check->kind != analysis::SuperCheckKind::kRequireInitialized)
    return false;
  const auto value = method.ssa.Write(access.instruction);
  const auto reads = method.ssa.Reads({i + 1});
  return value && reads.size() == 1 && reads.front().value == *value;
}
}  // namespace

std::variant<analysis::DefinedCaptures, SourceFailure> ProveDefinedCaptures(
    const abc::MethodBody& body, const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, std::uint64_t frame_size,
    const MethodConstants& constants, analysis::SemanticBudget budget) {
  using analysis::DefinedCaptures;
  const SourceFailure resource{SourceError::kResourceLimit, {}};
  const auto initial_work = budget.work;
  const auto charge = [&](std::uint64_t work = 1) {
    if (work > budget.work) throw resource;
    budget.work -= work;
  };
  const auto empty = [&] {
    DefinedCaptures result;
    result.work_units = initial_work - budget.work;
    return result;
  };
  try {
    std::vector<std::optional<analysis::InstructionRank>> site_order;
    bool candidate = false;
    for (const auto& access : proof.locals.accesses) {
      charge();
      candidate |= GuardedLoad(method, access);
    }
    if (!candidate) return empty();
    {
      const auto preserved = ProveCapturePreservation(
          body, constants, constants.leaf_methods, budget);
      const auto* sites = std::get_if<CapturePreservations>(&preserved);
      if (!sites) return std::get<SourceFailure>(preserved);
      charge(sites->work_units);
      if (sites->AllocatedBytes() > budget.bytes) return resource;
      auto local = analysis::BuildLocalSsa(body, method.flow, method.ssa,
          frame_size, constants.header.profile,
          {budget.bytes - sites->AllocatedBytes()}, budget.work, sites->sites);
      auto* cells = std::get_if<analysis::LocalSsa>(&local);
      if (!cells) {
        if (std::get<analysis::LocalSsaError>(local) ==
            analysis::LocalSsaError::kResourceLimit) return resource;
        return empty();
      }
      charge(cells->work_units);
      // This also certifies no raw environment escape and no re-entry. All
      // creations must preserve incoming cells, including descendants/buffers.
      if (cells->unpreserved_capture) return empty();
      site_order = std::move(cells->site_order);
      if (site_order.capacity() > budget.bytes / sizeof(std::optional<analysis::InstructionRank>)) return resource;
      budget.bytes -= site_order.capacity() * sizeof(std::optional<analysis::InstructionRank>);
    }
    const auto count = proof.locals.cells.size();
    if (count > budget.bytes / sizeof(std::optional<analysis::InstructionRank>))
      return resource;
    std::vector<std::optional<analysis::InstructionRank>> last_store(count);
    if (last_store.capacity() >
        budget.bytes / sizeof(std::optional<analysis::InstructionRank>)) return resource;
    budget.bytes -= last_store.capacity() * sizeof(std::optional<analysis::InstructionRank>);
    for (const auto& access : proof.locals.accesses) {
      charge();
      if (access.cell.value >= count || access.instruction.value >= site_order.size()) return empty();
      if (access.kind == analysis::LocalAccessKind::kStore) {
        const auto rank = site_order[access.instruction.value];
        if (rank && (!last_store[access.cell.value] || *last_store[access.cell.value] < *rank))
          last_store[access.cell.value] = rank;
      }
    }
    DefinedCaptures result;
    if (proof.locals.accesses.size() > budget.bytes / sizeof(analysis::CaptureGuard))
      return resource;
    result.guards.reserve(proof.locals.accesses.size());
    if (result.AllocatedBytes() > budget.bytes) return resource;
    budget.bytes -= result.AllocatedBytes();
    if (method.flow.blocks.size() > budget.bytes / sizeof(std::uint32_t))
      return resource;
    result.block_entries.resize(method.flow.blocks.size());
    if (result.block_entries.capacity() > budget.bytes / sizeof(std::uint32_t))
      return resource;
    budget.bytes -= result.block_entries.capacity() * sizeof(std::uint32_t);
    const auto built = analysis::BuildNormalDominators(method.flow, {}, {budget.bytes});
    const auto* tree = std::get_if<analysis::DominatorTree>(&built);
    if (!tree) {
      if (std::get<analysis::DominatorError>(built) ==
          analysis::DominatorError::kResourceLimit) return resource;
      return empty();
    }
    charge(tree->work.edge_visits);
    charge(tree->work.eval_steps);
    for (std::size_t b = 0; b < tree->nodes.size(); ++b) {
      charge();
      result.block_entries[b] = tree->nodes[b].begin;
    }
    std::size_t block = 0;
    for (const auto& access : proof.locals.accesses) {
      charge();
      if (!GuardedLoad(method, access)) continue;
      const auto i = access.instruction.value;
      const auto guard = analysis::InstructionId{i + 1};
      while (block + 1 < method.flow.blocks.size() &&
             method.flow.blocks[block + 1].first_instruction <= guard.value) {
        charge();
        ++block;
      }
      const auto& scope = tree->nodes[block];
      const auto store = last_store[access.cell.value];
      // DAG rank, not PC, proves no direct write can follow this guard. This
      // sufficient suffix fact does not equate incomparable execution histories.
      if (guard.value >= site_order.size() || !site_order[guard.value] ||
          !store || *store >= *site_order[guard.value] || !scope.begin ||
          method.flow.blocks[block].first_instruction > i) continue;
      result.guards.push_back({access.cell, guard, scope.begin, scope.end});
    }
    std::sort(result.guards.begin(), result.guards.end(), [&](auto a, auto b) {
      charge();
      return std::tuple(a.cell.value, a.begin, a.instruction.value) <
             std::tuple(b.cell.value, b.begin, b.instruction.value);
    });
    // Dominator subtrees are nested or disjoint. Keep outermost intervals;
    // equal intervals keep the earliest guard for same-block creation queries.
    std::size_t kept = 0;
    for (const auto guard : result.guards) {
      charge();
      if (kept && result.guards[kept - 1].cell.value == guard.cell.value &&
          guard.begin < result.guards[kept - 1].end) continue;
      result.guards[kept++] = guard;
    }
    result.guards.resize(kept);
    result.work_units = initial_work - budget.work;
    return result;
  } catch (const SourceFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return resource;
  } catch (const std::length_error&) { return resource; }
}
}  // namespace abcd::source
