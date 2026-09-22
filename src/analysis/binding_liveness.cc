#include <algorithm>
#include <map>
#include <memory_resource>
#include <new>

#include "abcd/analysis/phi_placement.h"
#include "idf_scratch.h"

namespace abcd::analysis {
namespace {
struct LiveMark {
  bool definition = false;
  bool live = false;
};
}  // namespace

std::variant<BindingLiveness, IdfError> IdfIndex::LiveIn(
    std::span<const BlockId> definitions, std::span<const BlockId> upward_uses,
    IdfQuota& quota, IdfBudget budget) const {
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw detail::IdfWorkLimit{};
    };
    detail::IdfScratch memory{budget.bytes};
    std::pmr::map<BlockId, LiveMark> marks{&memory};
    std::pmr::vector<BlockId> live{&memory};
    const auto count = nodes_ ? count_ : 0;
    for (const auto id : definitions) {
      charge();
      if (id.value >= count) return IdfError::kInvalidBlock;
      if (nodes_[id.value].preorder != 0) marks[id].definition = true;
    }
    for (const auto id : upward_uses) {
      charge();
      if (id.value >= count) return IdfError::kInvalidBlock;
      if (nodes_[id.value].preorder == 0) continue;
      auto& mark = marks[id];
      if (mark.live) continue;
      // A use before a definition needs the incoming value even in a defining
      // block. Definition kills apply only to backward propagation, not seeds.
      mark.live = true;
      live.push_back(id);
    }
    BindingLiveness result;
    for (std::size_t cursor = 0; cursor < live.size(); ++cursor) {
      charge();
      ++result.work.block_visits;
      const auto& node = nodes_[live[cursor].value];
      for (std::size_t i = 0; i < node.pred_count; ++i) {
        charge();
        ++result.work.edge_visits;
        const auto predecessor = predecessors_[node.pred_begin + i];
        auto& mark = marks[predecessor];
        if (mark.definition || mark.live) continue;
        mark.live = true;
        live.push_back(predecessor);
      }
    }
    result.work.marked_blocks = marks.size();
    charge(live.size());
    std::sort(live.begin(), live.end());
    if (live.size() > (budget.bytes - memory.Live()) / sizeof(BlockId))
      return IdfError::kResourceLimit;
    result.count_ = live.size();
    if (!live.empty()) {
      result.blocks_ = std::make_unique<BlockId[]>(live.size());
      std::copy(live.begin(), live.end(), result.blocks_.get());
    }
    result.work.peak_bytes =
        std::max(memory.Peak(), memory.Live() + result.AllocatedBytes());
    result.work.units = quota.Consumed() - consumed;
    return result;
  } catch (const detail::IdfWorkLimit&) {
    return IdfError::kWorkLimit;
  } catch (const std::bad_alloc&) {
    return IdfError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
