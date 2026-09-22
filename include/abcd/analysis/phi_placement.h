#ifndef ABCD_ANALYSIS_PHI_PLACEMENT_H_
#define ABCD_ANALYSIS_PHI_PLACEMENT_H_

#include <memory>
#include <optional>
#include <span>

#include "abcd/analysis/normal_flow.h"

namespace abcd::analysis {
enum class IdfError {
  kEmptyGraph,
  kInvalidEntry,
  kInvalidEdge,
  kResumeEdge,
  kInvalidBlock,
  kWorkLimit,
  kResourceLimit
};
// Reuse ONE quota across all binding queries of a method. Do not replenish it
// per variable: overlapping candidate sets can otherwise repeat quadratic
// work. Independent workers need separately admitted quotas, not a shared
// unsynchronized object. Failed queries keep their consumed units charged.
class IdfQuota {
 public:
  explicit IdfQuota(std::uint64_t units) : limit_(units), remaining_(units) {}
  IdfQuota(const IdfQuota&) = delete;
  IdfQuota& operator=(const IdfQuota&) = delete;
  [[nodiscard]] std::uint64_t Remaining() const { return remaining_; }
  [[nodiscard]] std::uint64_t Consumed() const { return limit_ - remaining_; }

 private:
  friend class IdfIndex;
  bool Take(std::uint64_t units) {
    if (units > remaining_) {
      remaining_ = 0;
      return false;
    }
    remaining_ -= units;
    return true;
  }
  std::uint64_t limit_;
  std::uint64_t remaining_;
};
struct IdfBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};
struct IdfWork {
  std::uint64_t block_visits = 0;
  std::uint64_t edge_visits = 0;
  std::uint64_t heap_pops = 0;
  std::uint64_t range_visits = 0;
  std::uint64_t units = 0;
  std::size_t marked_blocks = 0;
  std::size_t peak_bytes = 0;
};
struct LivenessWork {
  std::uint64_t block_visits = 0;
  std::uint64_t edge_visits = 0;
  std::uint64_t units = 0;
  std::size_t marked_blocks = 0;
  std::size_t peak_bytes = 0;
};
class BindingLiveness {
 public:
  BindingLiveness(BindingLiveness&&) noexcept = default;
  BindingLiveness& operator=(BindingLiveness&&) noexcept = default;
  [[nodiscard]] std::span<const BlockId> Blocks() const {
    return {blocks_.get(), blocks_ ? count_ : 0};
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return Blocks().size_bytes();
  }
  LivenessWork work;

 private:
  friend class IdfIndex;
  BindingLiveness() = default;
  std::unique_ptr<BlockId[]> blocks_;
  std::size_t count_ = 0;
};
class PhiPlacement {
 public:
  PhiPlacement(PhiPlacement&&) noexcept = default;
  PhiPlacement& operator=(PhiPlacement&&) noexcept = default;
  [[nodiscard]] std::span<const BlockId> Blocks() const {
    return {blocks_.get(), blocks_ ? count_ : 0};
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return Blocks().size_bytes();
  }
  IdfWork work;

 private:
  friend class IdfIndex;
  PhiPlacement() = default;
  std::unique_ptr<BlockId[]> blocks_;
  std::size_t count_ = 0;
};

// Owned normal-CFG/dominator snapshot. Build once per unchanged graph; supports
// independent concurrent Place calls without shared mutable scratch. No handler
// edges are inferred; resumes are rejected. This is not a function task
// service.
class IdfIndex {
 public:
  IdfIndex(IdfIndex&&) noexcept = default;
  IdfIndex& operator=(IdfIndex&&) noexcept = default;
  [[nodiscard]] static std::variant<IdfIndex, IdfError> Build(
      const NormalFlow& graph, BlockId entry = {}, IdfBudget budget = {});
  // Upward uses read the old value BEFORE any definition in that block.
  // A block in both spans is live-in. Later reads after a first definition are
  // NOT seeds. Normal-reachable input only; invalid IDs fail, duplicates and
  // unreachable IDs are ignored. O((D+U+L+E_L) log(2+D+U+L)), sparse storage;
  // each live block and incoming edge visited once. Does not derive def/use or
  // model closure aliasing. Reuse quota across this and Place for all bindings.
  [[nodiscard]] std::variant<BindingLiveness, IdfError> LiveIn(
      std::span<const BlockId> definitions,
      std::span<const BlockId> upward_uses, IdfQuota& quota,
      IdfBudget budget = {}) const;
  // One binding's IDF, optionally pruned by caller-proven live-in blocks.
  // nullopt is unpruned; empty span prunes all. Duplicates are accepted;
  // unreachable IDs are ignored, invalid IDs fail. Result is sorted and owned.
  // No B-sized query reset or unfiltered subtree walk. Range-minimum reporting
  // and disjoint subtree coverage give O((D+L+M+F) log(2+B+D+L)) work: D/L
  // supplied IDs, M candidate source blocks, F placed blocks. Degree is at most
  // two; each source is inspected at most once per query. Cross-variable
  // revisits and liveness/renaming remain separate; see phi-placement.md.
  [[nodiscard]] std::variant<PhiPlacement, IdfError> Place(
      std::span<const BlockId> definitions, IdfQuota& quota,
      std::optional<std::span<const BlockId>> live_in = {},
      IdfBudget budget = {}) const;
  [[nodiscard]] std::size_t AllocatedBytes() const;
  [[nodiscard]] std::size_t PeakBytes() const {
    return nodes_ ? peak_bytes_ : 0;
  }

 private:
  IdfIndex() = default;
  static constexpr std::uint32_t kNoDepth = UINT32_MAX;
  struct Node {
    std::array<BlockId, kMaxNormalSuccessors> successors{};
    std::uint32_t depth = 0;
    std::uint32_t preorder = 0;
    std::uint32_t end = 0;
    std::uint32_t pred_begin = 0;
    std::uint32_t pred_count = 0;
    std::uint8_t successor_count = 0;
  };
  std::unique_ptr<Node[]> nodes_;
  std::unique_ptr<BlockId[]> preorder_;
  std::unique_ptr<std::uint32_t[]> min_depth_;
  std::unique_ptr<BlockId[]> predecessors_;
  std::size_t count_ = 0;
  std::size_t edges_ = 0;
  std::size_t leaves_ = 0;
  std::size_t peak_bytes_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_PHI_PLACEMENT_H_
