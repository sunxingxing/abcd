#include "abcd/analysis/phi_placement.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <random>

#include "abcd/analysis/dominators.h"

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
// Small-graph oracle from the mathematical DF definition, intentionally not
// the production deepest-first algorithm. Never run it on scaling fixtures.
void Compare(const NormalFlow& graph, const DominatorTree& tree,
             const IdfIndex& index, std::span<const BlockId> definitions,
             std::optional<std::span<const BlockId>> live = {}) {
  const auto size = graph.blocks.size();
  std::vector<bool> defined(size), placed(size), live_in(size, !live);
  for (const auto id : definitions)
    if (tree.nodes[id.value].begin != 0) defined[id.value] = true;
  if (live)
    for (const auto id : *live) live_in[id.value] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::uint32_t a = 0; a < size; ++a) {
      if (!defined[a]) continue;
      for (std::uint32_t pred = 0; pred < size; ++pred) {
        if (!tree.Dominates({a}, {pred})) continue;
        const auto& block = graph.blocks[pred];
        for (std::size_t e = 0; e < block.successor_count; ++e) {
          const auto target = block.successors[e].target;
          if (!live_in[target.value] ||
              (target.value != a && tree.Dominates({a}, target)))
            continue;
          placed[target.value] = true;
          if (!defined[target.value]) {
            changed = true;
            defined[target.value] = true;
          }
        }
      }
    }
  }
  std::vector<BlockId> expected;
  for (std::uint32_t i = 0; i < size; ++i)
    if (placed[i]) expected.push_back({i});
  IdfQuota quota{10000};
  const auto actual =
      std::get<PhiPlacement>(index.Place(definitions, quota, live));
  Check(std::ranges::equal(actual.Blocks(), expected));
  Check(actual.work.block_visits <= 2 * size);
  Check(actual.work.edge_visits <= 2 * kMaxNormalSuccessors * size);
  Check(actual.work.heap_pops <= size);
}
}  // namespace

int main() {
  using namespace abcd::analysis;
  constexpr std::uint64_t kTestSteps = 100000000;
  IdfQuota quota{kTestSteps};
  NormalFlow graph;
  graph.blocks.resize(4);
  Edge(graph, 0, 1);
  Edge(graph, 0, 2);
  Edge(graph, 1, 3);
  Edge(graph, 2, 3);
  auto index = std::get<IdfIndex>(IdfIndex::Build(graph));
  const std::array<BlockId, 1> definitions{{{1}}};
  const auto placement =
      std::get<PhiPlacement>(index.Place(definitions, quota));
  Check(placement.Blocks().size() == 1 && placement.Blocks()[0].value == 3);
  IdfQuota exact_work{placement.work.units},
      short_work{placement.work.units - 1}, no_work{0};
  Check(std::holds_alternative<PhiPlacement>(
      index.Place(definitions, exact_work)));
  Check(exact_work.Remaining() == 0 &&
        exact_work.Consumed() == placement.work.units);
  Check(std::get<IdfError>(index.Place(definitions, short_work)) ==
        IdfError::kWorkLimit);
  Check(short_work.Remaining() == 0);
  Check(std::get<IdfError>(index.Place(definitions, no_work)) ==
        IdfError::kWorkLimit);
  Check(std::get<PhiPlacement>(index.Place({}, no_work)).work.units == 0);
  IdfQuota failed_memory{100};
  Check(std::get<IdfError>(index.Place(definitions, failed_memory, {}, {0})) ==
        IdfError::kResourceLimit);
  Check(failed_memory.Consumed() != 0);
  Check(std::get<PhiPlacement>(
            index.Place(definitions, quota, std::span<const BlockId>{}))
            .Blocks()
            .empty());
  const std::array<BlockId, 3> duplicates{{{1}, {1}, {1}}};
  Check(std::ranges::equal(
      std::get<PhiPlacement>(index.Place(duplicates, quota)).Blocks(),
      placement.Blocks()));
  Check(std::get<IdfError>(index.Place(definitions, quota, {}, {0})) ==
        IdfError::kResourceLimit);
  Check(std::holds_alternative<PhiPlacement>(
      index.Place(definitions, quota, {}, {placement.work.peak_bytes})));
  Check(std::get<IdfError>(index.Place(definitions, quota, {},
                                       {placement.work.peak_bytes - 1})) ==
        IdfError::kResourceLimit);
  const std::array<BlockId, 1> invalid{{{4}}};
  Check(std::get<IdfError>(index.Place(invalid, quota)) ==
        IdfError::kInvalidBlock);
  Check(std::get<IdfError>(index.Place({}, quota, invalid)) ==
        IdfError::kInvalidBlock);
  Check(
      std::get<PhiPlacement>(index.Place({}, quota, {}, {0})).Blocks().empty());
  auto moved = std::move(index);
  Check(index.AllocatedBytes() == 0 && index.PeakBytes() == 0);
  Check(std::get<IdfError>(index.Place(definitions, quota)) ==
        IdfError::kInvalidBlock);
  Check(std::get<PhiPlacement>(index.Place({}, quota)).Blocks().empty());
  auto movable = std::get<PhiPlacement>(moved.Place(definitions, quota));
  auto retained = std::move(movable);
  Check(movable.Blocks().empty() && movable.AllocatedBytes() == 0);
  Check(retained.Blocks().size() == 1);
  graph.blocks.clear();
  Check(std::ranges::equal(
      std::get<PhiPlacement>(moved.Place(definitions, quota)).Blocks(),
      placement.Blocks()));

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
      const auto tree =
          std::get<DominatorTree>(BuildNormalDominators(small, {entry}));
      for (std::uint32_t defs = 0; defs < (1U << kSmall); ++defs) {
        const auto inputs = Subset(defs, kSmall);
        Compare(small, tree, snapshot, inputs);
        for (std::uint32_t live = 0; live < (1U << kSmall); ++live) {
          const auto live_in = Subset(live, kSmall);
          Compare(small, tree, snapshot, inputs, live_in);
        }
      }
    }
  }
  std::mt19937 random(0x1df);
  for (std::uint32_t trial = 0; trial < 500; ++trial) {
    NormalFlow sample;
    sample.blocks.resize(16);
    for (std::uint32_t a = 0; a < sample.blocks.size(); ++a) {
      const auto degree = random() % (kMaxNormalSuccessors + 1);
      for (std::size_t e = 0; e < degree; ++e)
        Edge(sample, a, random() % sample.blocks.size());
    }
    const BlockId entry{static_cast<std::uint32_t>(random() % 16)};
    const auto snapshot = std::get<IdfIndex>(IdfIndex::Build(sample, entry));
    const auto tree =
        std::get<DominatorTree>(BuildNormalDominators(sample, entry));
    const auto inputs = Subset(random(), 16), live = Subset(random(), 16);
    Compare(sample, tree, snapshot, inputs);
    Compare(sample, tree, snapshot, inputs, live);
  }
  for (const std::uint32_t count : {200, 201, 4096, 65536}) {
    NormalFlow chain;
    chain.blocks.resize(count);
    for (std::uint32_t i = 0; i + 1 < count; ++i) Edge(chain, i, i + 1);
    const auto snapshot = std::get<IdfIndex>(IdfIndex::Build(chain));
    Check(std::holds_alternative<IdfIndex>(
        IdfIndex::Build(chain, {}, {snapshot.PeakBytes()})));
    Check(std::get<IdfError>(
              IdfIndex::Build(chain, {}, {snapshot.PeakBytes() - 1})) ==
          IdfError::kResourceLimit);
    const std::array<BlockId, 1> root{{{0}}}, leaf{{{count - 1}}};
    const auto whole = std::get<PhiPlacement>(snapshot.Place(root, quota));
    Check(whole.Blocks().empty() && whole.work.block_visits == 0);
    Check(whole.work.edge_visits == 0 && whole.work.range_visits == 1);
    for (std::size_t query = 0; query < count; ++query) {
      const auto sparse = std::get<PhiPlacement>(snapshot.Place(leaf, quota));
      Check(sparse.Blocks().empty() && sparse.work.block_visits == 0);
      Check(sparse.work.marked_blocks == 1 && sparse.work.edge_visits == 0);
      Check(sparse.work.range_visits <= 65);
      Check(sparse.work.peak_bytes < 1024);
      const auto entry_query =
          std::get<PhiPlacement>(snapshot.Place(root, quota));
      Check(entry_query.work.range_visits == 1 &&
            entry_query.work.edge_visits == 0);
    }
    Edge(chain, count - 1, 0);
    const auto loop = std::get<IdfIndex>(IdfIndex::Build(chain));
    const auto loop_result = std::get<PhiPlacement>(loop.Place(leaf, quota));
    Check(loop_result.Blocks().size() == 1 &&
          loop_result.Blocks()[0].value == 0);
    Check(loop_result.work.block_visits <= 2ULL * count);
    Check(loop_result.work.edge_visits <= 2ULL * count);

    // Reverse physical order, nested definition subtrees and a frontier at
    // every block: stresses interval merging, queue/output growth and pruning.
    NormalFlow nested;
    nested.blocks.resize(count);
    std::vector<BlockId> all, even;
    for (std::uint32_t i = 0; i < count; ++i) {
      Edge(nested, i, i);
      if (i != 0) Edge(nested, i, i - 1);
      all.push_back({i});
      if (i % 2 == 0) even.push_back({i});
    }
    const auto nested_index =
        std::get<IdfIndex>(IdfIndex::Build(nested, {count - 1}));
    const auto dense = std::get<PhiPlacement>(nested_index.Place(all, quota));
    Check(std::ranges::equal(dense.Blocks(), all));
    Check(dense.work.block_visits == count &&
          dense.work.edge_visits == 2ULL * count - 1);
    Check(dense.work.range_visits <= 65ULL * count);
    Check(dense.AllocatedBytes() == count * sizeof(BlockId));
    Check(std::ranges::equal(
        std::get<PhiPlacement>(nested_index.Place(all, quota, even)).Blocks(),
        even));
    Check(std::holds_alternative<PhiPlacement>(
        nested_index.Place(all, quota, {}, {dense.work.peak_bytes})));
    Check(std::get<IdfError>(nested_index.Place(all, quota, {},
                                                {dense.work.peak_bytes - 1})) ==
          IdfError::kResourceLimit);
  }
  for (const std::uint32_t count : {200, 201, 4096, 65536}) {
    // Many edges can produce just one phi. RMQ cannot collapse repeated
    // targets: retain honest counts and require shared batch admission.
    NormalFlow backedges;
    backedges.blocks.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      Edge(backedges, i, 0);
      if (i + 1 < count) Edge(backedges, i, i + 1);
    }
    const auto back_index = std::get<IdfIndex>(IdfIndex::Build(backedges));
    const std::array<BlockId, 1> root{{{0}}};
    const auto one_phi = std::get<PhiPlacement>(back_index.Place(root, quota));
    Check(one_phi.Blocks().size() == 1 && one_phi.Blocks()[0].value == 0);
    Check(one_phi.work.block_visits == count);
    Check(one_phi.work.edge_visits == 2ULL * count - 1);
    IdfQuota batch{one_phi.work.units + 1};
    std::size_t completed = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::array<BlockId, 1> definition{{{i}}};
      auto next = back_index.Place(definition, batch);
      if (const auto* error = std::get_if<IdfError>(&next)) {
        Check(*error == IdfError::kWorkLimit);
        break;
      }
      ++completed;
    }
    Check(completed == 1 && batch.Remaining() == 0);
  }
  NormalFlow invalid_graph;
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph)) ==
        IdfError::kEmptyGraph);
  invalid_graph.blocks.resize(1);
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph, {1})) ==
        IdfError::kInvalidEntry);
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph, {}, {0})) ==
        IdfError::kResourceLimit);
  Edge(invalid_graph, 0, 0);
  invalid_graph.blocks[0].successors[0].kind = static_cast<FlowEdgeKind>(255);
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph)) ==
        IdfError::kInvalidEdge);
  invalid_graph.blocks[0].successors[0].kind = FlowEdgeKind::kResume;
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph)) ==
        IdfError::kResumeEdge);
  invalid_graph.blocks[0].successors[0] = {{1}, FlowEdgeKind::kTaken};
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph)) ==
        IdfError::kInvalidEdge);
  invalid_graph.blocks[0].successor_count = kMaxNormalSuccessors + 1;
  Check(std::get<IdfError>(IdfIndex::Build(invalid_graph)) ==
        IdfError::kInvalidEdge);
}
