#include <algorithm>
#include <array>
#include <cstdlib>
#include <random>

#include "abcd/analysis/phi_placement.h"

namespace {
using namespace abcd::analysis;
void Check(bool value) {
  if (!value) std::abort();
}
void Edge(NormalFlow& graph, std::uint32_t from, std::uint32_t to) {
  auto& block = graph.blocks[from];
  block.successors[block.successor_count++] = {{to}, FlowEdgeKind::kTaken};
}
std::vector<BlockId> Subset(std::uint32_t bits, std::uint32_t count) {
  std::vector<BlockId> result;
  for (std::uint32_t i = 0; i < count; ++i)
    if (bits & (1U << i)) result.push_back({i});
  return result;
}
void Compare(const NormalFlow& graph, const IdfIndex& index, BlockId entry,
             std::span<const BlockId> definitions,
             std::span<const BlockId> uses) {
  const auto size = graph.blocks.size();
  std::vector<bool> reachable(size), defined(size), upward(size), live(size);
  std::vector<BlockId> pending{entry};
  reachable[entry.value] = true;
  for (std::size_t i = 0; i < pending.size(); ++i) {
    const auto& block = graph.blocks[pending[i].value];
    for (std::size_t e = 0; e < block.successor_count; ++e) {
      const auto target = block.successors[e].target;
      if (!reachable[target.value]) {
        reachable[target.value] = true;
        pending.push_back(target);
      }
    }
  }
  for (const auto id : definitions) defined[id.value] = true;
  for (const auto id : uses) upward[id.value] = true;
  // Deliberately dense small-graph fixed point, independent of the production
  // predecessor FIFO. Never invoke this oracle on the scaling fixtures.
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::uint32_t b = 0; b < size; ++b) {
      if (!reachable[b] || live[b]) continue;
      bool next = upward[b];
      if (!defined[b]) {
        const auto& block = graph.blocks[b];
        for (std::size_t e = 0; e < block.successor_count; ++e)
          next = next || live[block.successors[e].target.value];
      }
      if (next) live[b] = changed = true;
    }
  }
  std::vector<BlockId> expected;
  std::uint64_t edges = 0;
  for (std::uint32_t b = 0; b < size; ++b) {
    if (live[b]) expected.push_back({b});
    if (!reachable[b]) continue;
    const auto& block = graph.blocks[b];
    for (std::size_t e = 0; e < block.successor_count; ++e)
      if (live[block.successors[e].target.value]) ++edges;
  }
  IdfQuota quota{10000};
  const auto result =
      std::get<BindingLiveness>(index.LiveIn(definitions, uses, quota));
  Check(std::ranges::equal(result.Blocks(), expected));
  Check(result.work.block_visits == expected.size() &&
        result.work.edge_visits == edges);
  Check(result.work.units == quota.Consumed());
}
}  // namespace

int main() {
  using namespace abcd::analysis;
  NormalFlow graph;
  graph.blocks.resize(4);
  Edge(graph, 0, 1);
  Edge(graph, 0, 2);
  Edge(graph, 1, 3);
  Edge(graph, 2, 3);
  auto index = std::get<IdfIndex>(IdfIndex::Build(graph));
  const std::array<BlockId, 3> defs{{{1}, {2}, {3}}};
  const std::array<BlockId, 1> uses{{{3}}};
  IdfQuota quota{10000};
  const auto live = std::get<BindingLiveness>(index.LiveIn(defs, uses, quota));
  Check(live.Blocks().size() == 1 && live.Blocks()[0].value == 3);
  const auto placed =
      std::get<PhiPlacement>(index.Place(defs, quota, live.Blocks()));
  Check(placed.Blocks().size() == 1 && placed.Blocks()[0].value == 3);
  IdfQuota combined{live.work.units + placed.work.units};
  const auto combined_live =
      std::get<BindingLiveness>(index.LiveIn(defs, uses, combined));
  Check(std::holds_alternative<PhiPlacement>(
      index.Place(defs, combined, combined_live.Blocks())));
  Check(combined.Remaining() == 0);
  const auto killed = std::get<BindingLiveness>(index.LiveIn(defs, {}, quota));
  Check(killed.Blocks().empty());
  Check(std::get<PhiPlacement>(index.Place(defs, quota, killed.Blocks()))
            .Blocks()
            .empty());
  const std::array<BlockId, 2> duplicates{{{3}, {3}}};
  Check(std::ranges::equal(
      std::get<BindingLiveness>(index.LiveIn(defs, duplicates, quota)).Blocks(),
      live.Blocks()));
  IdfQuota exact{live.work.units}, short_work{live.work.units - 1}, zero{0};
  Check(
      std::holds_alternative<BindingLiveness>(index.LiveIn(defs, uses, exact)));
  Check(exact.Remaining() == 0);
  Check(std::get<IdfError>(index.LiveIn(defs, uses, short_work)) ==
        IdfError::kWorkLimit);
  Check(short_work.Remaining() == 0);
  Check(std::get<IdfError>(index.LiveIn(defs, uses, zero)) ==
        IdfError::kWorkLimit);
  Check(std::get<BindingLiveness>(index.LiveIn({}, {}, zero, {0}))
            .Blocks()
            .empty());
  Check(std::holds_alternative<BindingLiveness>(
      index.LiveIn(defs, uses, quota, {live.work.peak_bytes})));
  Check(std::get<IdfError>(
            index.LiveIn(defs, uses, quota, {live.work.peak_bytes - 1})) ==
        IdfError::kResourceLimit);
  IdfQuota failed_memory{100};
  Check(std::get<IdfError>(index.LiveIn(defs, uses, failed_memory, {0})) ==
        IdfError::kResourceLimit);
  Check(failed_memory.Consumed() != 0);
  const std::array<BlockId, 1> invalid{{{4}}};
  Check(std::get<IdfError>(index.LiveIn(invalid, {}, quota)) ==
        IdfError::kInvalidBlock);
  Check(std::get<IdfError>(index.LiveIn({}, invalid, quota)) ==
        IdfError::kInvalidBlock);
  auto owned = std::move(index);
  graph.blocks.clear();
  Check(std::get<IdfError>(index.LiveIn({}, uses, quota)) ==
        IdfError::kInvalidBlock);
  Check(
      std::get<BindingLiveness>(index.LiveIn({}, {}, quota)).Blocks().empty());
  auto movable = std::get<BindingLiveness>(owned.LiveIn(defs, uses, quota));
  auto retained = std::move(movable);
  Check(movable.Blocks().empty() && movable.AllocatedBytes() == 0);
  Check(std::ranges::equal(retained.Blocks(), live.Blocks()));
  auto detached = [] {
    NormalFlow local;
    local.blocks.resize(1);
    auto snapshot = std::get<IdfIndex>(IdfIndex::Build(local));
    const std::array<BlockId, 1> seed{{{0}}};
    IdfQuota local_quota{100};
    return std::get<BindingLiveness>(snapshot.LiveIn({}, seed, local_quota));
  }();
  Check(detached.Blocks().size() == 1 && detached.Blocks()[0].value == 0);
  detached = std::move(retained);
  Check(retained.Blocks().empty() && retained.AllocatedBytes() == 0);
  Check(std::ranges::equal(detached.Blocks(), live.Blocks()));

  constexpr std::uint32_t kSmall = 3;
  for (std::uint32_t mask = 0; mask < (1U << (kSmall * kSmall)); ++mask) {
    NormalFlow small;
    small.blocks.resize(kSmall);
    bool valid = true;
    for (std::uint32_t a = 0; a < kSmall; ++a)
      for (std::uint32_t b = 0; b < kSmall; ++b)
        if (mask & (1U << (a * kSmall + b))) {
          if (small.blocks[a].successor_count == kMaxNormalSuccessors)
            valid = false;
          else
            Edge(small, a, b);
        }
    if (!valid) continue;
    for (std::uint32_t entry = 0; entry < kSmall; ++entry) {
      const auto snapshot = std::get<IdfIndex>(IdfIndex::Build(small, {entry}));
      for (std::uint32_t writes = 0; writes < (1U << kSmall); ++writes)
        for (std::uint32_t reads = 0; reads < (1U << kSmall); ++reads)
          Compare(small, snapshot, {entry}, Subset(writes, kSmall),
                  Subset(reads, kSmall));
    }
  }
  std::mt19937 random(0x11fe);
  for (std::uint32_t trial = 0; trial < 1000; ++trial) {
    NormalFlow sample;
    sample.blocks.resize(16);
    for (std::uint32_t b = 0; b < sample.blocks.size(); ++b) {
      const auto degree = random() % (kMaxNormalSuccessors + 1);
      for (std::size_t e = 0; e < degree; ++e)
        Edge(sample, b, random() % sample.blocks.size());
    }
    const BlockId entry{static_cast<std::uint32_t>(random() % 16)};
    const auto snapshot = std::get<IdfIndex>(IdfIndex::Build(sample, entry));
    Compare(sample, snapshot, entry, Subset(random(), 16),
            Subset(random(), 16));
  }
  for (const std::uint32_t count : {200, 201, 4096, 65536}) {
    NormalFlow chain;
    chain.blocks.resize(count);
    for (std::uint32_t i = 0; i + 1 < count; ++i) Edge(chain, i, i + 1);
    const auto snapshot = std::get<IdfIndex>(IdfIndex::Build(chain));
    const std::array<BlockId, 1> leaf{{{count - 1}}}, kill{{{count - 2}}};
    constexpr std::uint64_t kBatchUnits = 10000000;
    IdfQuota batch{kBatchUnits};
    const auto whole =
        std::get<BindingLiveness>(snapshot.LiveIn({}, leaf, batch));
    Check(whole.Blocks().size() == count && whole.work.block_visits == count);
    Check(whole.work.edge_visits == count - 1);
    for (std::size_t i = 0; i < count; ++i) {
      const auto sparse =
          std::get<BindingLiveness>(snapshot.LiveIn(kill, leaf, batch));
      Check(sparse.Blocks().size() == 1 && sparse.Blocks()[0] == leaf[0]);
      Check(sparse.work.block_visits == 1 && sparse.work.edge_visits == 1);
      Check(sparse.work.marked_blocks == 2 && sparse.work.peak_bytes < 1024);
    }
    Check(std::holds_alternative<BindingLiveness>(
        snapshot.LiveIn({}, leaf, batch, {whole.work.peak_bytes})));
    Check(std::get<IdfError>(
              snapshot.LiveIn({}, leaf, batch, {whole.work.peak_bytes - 1})) ==
          IdfError::kResourceLimit);
    IdfQuota shared{whole.work.units};
    Check(std::holds_alternative<BindingLiveness>(
        snapshot.LiveIn({}, leaf, shared)));
    Check(std::get<IdfError>(snapshot.Place(kill, shared, whole.Blocks())) ==
          IdfError::kWorkLimit);
    Edge(chain, count - 1, 0);
    const auto loop = std::get<IdfIndex>(IdfIndex::Build(chain));
    const auto cycle = std::get<BindingLiveness>(loop.LiveIn({}, leaf, batch));
    Check(cycle.Blocks().size() == count && cycle.work.edge_visits == count);
    NormalFlow fan_in;
    fan_in.blocks.resize(count);
    std::vector<BlockId> barriers;
    for (std::uint32_t i = 0; i + 1 < count; ++i) {
      Edge(fan_in, i, i + 1);
      Edge(fan_in, i, count - 1);
      barriers.push_back({i});
    }
    const auto fan_index = std::get<IdfIndex>(IdfIndex::Build(fan_in));
    const auto fan_live =
        std::get<BindingLiveness>(fan_index.LiveIn(barriers, leaf, batch));
    Check(fan_live.Blocks().size() == 1 && fan_live.work.block_visits == 1);
    Check(fan_live.work.edge_visits == count);
  }
}
