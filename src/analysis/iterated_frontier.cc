// Adapted traversal strategy from LLVM llvmorg-19.1.7
// llvm/include/llvm/Support/GenericIteratedDominanceFrontier.h.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// See docs/licenses/llvm-19.1.7.txt and docs/design/phi-placement.md.
#include <algorithm>
#include <limits>
#include <map>
#include <memory_resource>
#include <new>
#include <tuple>

#include "abcd/analysis/phi_placement.h"

namespace abcd::analysis {
namespace {
class IdfScratch final : public std::pmr::memory_resource {
 public:
  explicit IdfScratch(std::size_t limit) : limit_(limit) {}
  std::size_t Live() const { return live_; }
  std::size_t Peak() const { return peak_; }

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > limit_ - live_) throw std::bad_alloc{};
    auto* memory = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live_ += bytes;
    peak_ = std::max(peak_, live_);
    return memory;
  }
  void do_deallocate(void* memory, std::size_t bytes,
                     std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    live_ -= bytes;
  }
  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
  std::size_t limit_;
  std::size_t live_ = 0;
  std::size_t peak_ = 0;
};
struct Marks {
  bool definition = false;
  bool live = false;
  bool frontier = false;
};
struct WorkLimit {};
}  // namespace

std::variant<PhiPlacement, IdfError> IdfIndex::Place(
    std::span<const BlockId> definitions, IdfQuota& quota,
    std::optional<std::span<const BlockId>> live_in, IdfBudget budget) const {
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw WorkLimit{};
    };
    IdfScratch memory{budget.bytes};
    std::pmr::map<BlockId, Marks> marks{&memory};
    std::pmr::map<std::uint32_t, std::uint32_t> covered{&memory};
    std::pmr::vector<BlockId> roots{&memory}, placed{&memory};
    const auto count = nodes_ ? count_ : 0;
    const auto shallower = [&](BlockId a, BlockId b) {
      return std::tie(nodes_[a.value].depth, nodes_[a.value].preorder) <
             std::tie(nodes_[b.value].depth, nodes_[b.value].preorder);
    };
    const auto enqueue = [&](BlockId id) {
      roots.push_back(id);
      std::push_heap(roots.begin(), roots.end(), shallower);
    };
    for (const auto id : definitions) {
      charge();
      if (id.value >= count) return IdfError::kInvalidBlock;
      if (nodes_[id.value].preorder == 0) continue;
      auto& mark = marks[id];
      if (mark.definition) continue;
      mark.definition = true;
      enqueue(id);
    }
    if (live_in)
      for (const auto id : *live_in) {
        charge();
        if (id.value >= count) return IdfError::kInvalidBlock;
        if (nodes_[id.value].preorder != 0) marks[id].live = true;
      }
    PhiPlacement result;
    const auto report = [&](std::uint32_t begin, std::uint32_t end,
                            std::uint32_t depth) {
      if (begin == end) return;
      struct Range {
        std::size_t node;
        std::size_t begin;
        std::size_t end;
      };
      // Explicit binary-tree DFS has at most one deferred sibling per level.
      // The bound follows the address-sized leaf domain, not input recursion.
      constexpr auto kRangeFrames =
          std::numeric_limits<std::size_t>::digits + 1;
      std::array<Range, kRangeFrames> stack{};
      std::size_t size = 1;
      stack[0] = {1, 0, leaves_};
      while (size != 0) {
        charge();
        const auto range = stack[--size];
        ++result.work.range_visits;
        if (range.end <= begin || range.begin >= end ||
            min_depth_[range.node] > depth)
          continue;
        if (range.end - range.begin != 1) {
          const auto middle = range.begin + (range.end - range.begin) / 2;
          stack[size++] = {2 * range.node + 1, middle, range.end};
          stack[size++] = {2 * range.node, range.begin, middle};
          continue;
        }
        const auto& node = nodes_[preorder_[range.begin].value];
        ++result.work.block_visits;
        for (std::size_t i = 0; i < node.successor_count; ++i) {
          charge();
          ++result.work.edge_visits;
          const auto target = node.successors[i];
          if (nodes_[target.value].depth > depth) continue;
          auto& mark = marks[target];
          if (mark.frontier) continue;
          mark.frontier = true;
          if (live_in && !mark.live) continue;
          placed.push_back(target);
          if (!mark.definition) enqueue(target);
        }
      }
    };
    while (!roots.empty()) {
      charge();
      const auto root = roots.front();
      std::pop_heap(roots.begin(), roots.end(), shallower);
      roots.pop_back();
      ++result.work.heap_pops;
      const auto& node = nodes_[root.value];
      const auto begin = node.preorder - 1;
      const auto end = node.end - 1;
      auto next = covered.upper_bound(begin);
      if (next != covered.begin() && std::prev(next)->second >= end) continue;
      auto cursor = begin;
      next = covered.lower_bound(begin);
      // Dominator subtrees are laminar. Deepest-first roots let us subtract
      // already processed descendant intervals, then merge them into this
      // root's interval. Each interval is inserted/erased at most once; source
      // vertices are never rescanned under a later shallower threshold.
      while (next != covered.end() && next->first < end) {
        charge();
        report(cursor, next->first, node.depth);
        cursor = next->second;
        next = covered.erase(next);
      }
      report(cursor, end, node.depth);
      covered.emplace(begin, end);
    }
    result.work.marked_blocks = marks.size();
    charge(placed.size());
    std::sort(placed.begin(), placed.end());
    if (placed.size() > (budget.bytes - memory.Live()) / sizeof(BlockId))
      return IdfError::kResourceLimit;
    result.count_ = placed.size();
    if (!placed.empty()) {
      result.blocks_ = std::make_unique<BlockId[]>(placed.size());
      std::copy(placed.begin(), placed.end(), result.blocks_.get());
    }
    result.work.peak_bytes =
        std::max(memory.Peak(), memory.Live() + result.AllocatedBytes());
    result.work.units = quota.Consumed() - consumed;
    return result;
  } catch (const WorkLimit&) {
    return IdfError::kWorkLimit;
  } catch (const std::bad_alloc&) {
    return IdfError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
