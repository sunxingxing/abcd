#include "abcd/analysis/dominators.h"

#include <algorithm>
#include <new>
#include <utility>

namespace abcd::analysis {
namespace {
constexpr std::uint32_t kAbsent = UINT32_MAX;
struct DfsNode {
  std::uint32_t number = 0;
  std::uint32_t parent = kAbsent;
  std::uint32_t semi = 0;
  std::uint32_t label = kAbsent;
  std::uint32_t ancestor = kAbsent;
  std::uint32_t idom = kAbsent;
  std::uint32_t bucket_head = kAbsent;
  std::uint32_t bucket_next = kAbsent;
  std::uint32_t child_head = kAbsent;
  std::uint32_t child_next = kAbsent;
  std::uint32_t pred_begin = 0;
  std::uint32_t pred_count = 0;
};
struct DfsFrame {
  std::uint32_t node = 0;
  std::uint32_t next = 0;
};

class DominatorBuilder {
 public:
  DominatorBuilder(const NormalFlow& graph, BlockId entry,
                   DominatorBudget budget)
      : graph_(graph), entry_(entry.value), budget_(budget.bytes),
        remaining_work_(budget.work) {}

  DominatorResult Build() {
    const auto count = graph_.blocks.size();
    if (count == 0) return DominatorError::kEmptyGraph;
    if (entry_ >= count) return DominatorError::kInvalidEntry;
    if (count >= kAbsent) return DominatorError::kResourceLimit;
    std::size_t edges = 0;
    for (const auto& block : graph_.blocks) {
      Step();
      if (block.successor_count > block.successors.size())
        return DominatorError::kInvalidEdge;
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        Step();
        ++tree_.work.edge_visits;
        if (block.successors[i].target.value >= count)
          return DominatorError::kInvalidEdge;
      }
      if (edges >= kAbsent - block.successor_count)
        return DominatorError::kResourceLimit;
      edges += block.successor_count;
    }
    if (edges >= kAbsent || !Allocate(tree_.nodes, count) ||
        !Allocate(nodes_, count) || !Allocate(order_, count) ||
        !Allocate(stack_, count) || !Allocate(path_, count) ||
        !Allocate(predecessors_, edges))
      return DominatorError::kResourceLimit;
    Discover();
    IndexPredecessors();
    ComputeParents();
    IndexTree();
    return std::move(tree_);
  }

 private:
  void Step(std::size_t count = 1) {
    if (remaining_work_) {
      if (count > *remaining_work_) throw DominatorError::kWorkLimit;
      *remaining_work_ -= count;
    }
    tree_.work.steps += count;
  }
  template <class T>
  bool Allocate(std::vector<T>& values, std::size_t count) {
    Step(count);
    auto& spent = tree_.work.allocated_bytes;
    if (count > values.max_size() || count > (budget_ - spent) / sizeof(T))
      return false;
    // Every vector is allocated exactly once; no old/new growing buffers.
    values.resize(count);
    if (values.capacity() > (budget_ - spent) / sizeof(T)) return false;
    spent += values.capacity() * sizeof(T);
    return true;
  }

  void Discover() {
    const auto discover = [&](std::uint32_t node) {
      order_[reachable_++] = node;
      nodes_[node].number = reachable_;
      nodes_[node].semi = reachable_;
      nodes_[node].label = node;
    };
    discover(entry_);
    std::size_t depth = 1;
    stack_[0] = {entry_, 0};
    while (depth != 0) {
      Step();
      auto& frame = stack_[depth - 1];
      const auto& block = graph_.blocks[frame.node];
      if (frame.next == block.successor_count) {
        --depth;
        continue;
      }
      const auto target = block.successors[frame.next++].target.value;
      ++tree_.work.edge_visits;
      if (nodes_[target].number != 0) continue;
      nodes_[target].parent = frame.node;
      discover(target);
      stack_[depth++] = {target, 0};
    }
  }

  void IndexPredecessors() {
    for (const auto& block : graph_.blocks) {
      Step();
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        Step();
        ++nodes_[block.successors[i].target.value].pred_count;
        ++tree_.work.edge_visits;
      }
    }
    std::uint32_t offset = 0;
    for (auto& node : nodes_) {
      Step();
      node.pred_begin = offset;
      offset += node.pred_count;
      node.pred_count = 0;
    }
    for (std::uint32_t from = 0; from < graph_.blocks.size(); ++from) {
      Step();
      const auto& block = graph_.blocks[from];
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        Step();
        auto& target = nodes_[block.successors[i].target.value];
        predecessors_[target.pred_begin + target.pred_count++] = from;
        ++tree_.work.edge_visits;
      }
    }
  }

  std::uint32_t Eval(std::uint32_t vertex) {
    std::size_t length = 0;
    auto cursor = vertex;
    while (nodes_[cursor].ancestor != kAbsent &&
           nodes_[nodes_[cursor].ancestor].ancestor != kAbsent) {
      Step();
      path_[length++] = cursor;
      cursor = nodes_[cursor].ancestor;
      ++tree_.work.eval_steps;
    }
    // Reverse the compress recursion. Updating ancestors on descent would lose
    // the minimum-semidominator label on the remaining path.
    while (length != 0) {
      Step();
      auto& node = nodes_[path_[--length]];
      const auto& ancestor = nodes_[node.ancestor];
      if (nodes_[ancestor.label].semi < nodes_[node.label].semi)
        node.label = ancestor.label;
      node.ancestor = ancestor.ancestor;
      ++tree_.work.eval_steps;
    }
    return nodes_[vertex].label;
  }

  void ComputeParents() {
    for (std::uint32_t i = reachable_ - 1; i != 0; --i) {
      Step();
      const auto vertex = order_[i];
      auto& node = nodes_[vertex];
      for (std::uint32_t j = 0; j < node.pred_count; ++j) {
        Step();
        const auto pred = predecessors_[node.pred_begin + j];
        ++tree_.work.edge_visits;
        if (nodes_[pred].number != 0)
          node.semi = std::min(node.semi, nodes_[Eval(pred)].semi);
      }
      auto& bucket = nodes_[order_[node.semi - 1]].bucket_head;
      node.bucket_next = bucket;
      bucket = vertex;
      node.ancestor = node.parent;  // LINK(parent, vertex), simple LT variant.
      auto& parent = nodes_[node.parent];
      auto pending = parent.bucket_head;
      parent.bucket_head = kAbsent;
      while (pending != kAbsent) {
        Step();
        auto& candidate = nodes_[pending];
        const auto label = Eval(pending);
        candidate.idom =
            nodes_[label].semi < candidate.semi ? label : node.parent;
        pending = candidate.bucket_next;
      }
    }
    for (std::uint32_t i = 1; i < reachable_; ++i) {
      Step();
      auto& node = nodes_[order_[i]];
      if (node.idom != order_[node.semi - 1])
        node.idom = nodes_[node.idom].idom;
      tree_.nodes[order_[i]].parent = BlockId{node.idom};
    }
  }

  void IndexTree() {
    for (std::uint32_t i = reachable_; i > 1; --i) {
      Step();
      const auto vertex = order_[i - 1];
      auto& node = nodes_[vertex];
      node.child_next = nodes_[node.idom].child_head;
      nodes_[node.idom].child_head = vertex;
    }
    std::uint32_t stamp = 1;
    tree_.nodes[entry_].begin = stamp++;
    std::size_t depth = 1;
    stack_[0] = {entry_, nodes_[entry_].child_head};
    while (depth != 0) {
      Step();
      auto& frame = stack_[depth - 1];
      if (frame.next == kAbsent) {
        tree_.nodes[frame.node].end = stamp;
        --depth;
      } else {
        const auto child = frame.next;
        frame.next = nodes_[child].child_next;
        tree_.nodes[child].begin = stamp++;
        stack_[depth++] = {child, nodes_[child].child_head};
      }
    }
  }

  const NormalFlow& graph_;
  std::uint32_t entry_;
  std::size_t budget_;
  std::optional<std::uint64_t> remaining_work_;
  DominatorTree tree_;
  std::vector<DfsNode> nodes_;
  std::vector<std::uint32_t> order_, path_, predecessors_;
  std::vector<DfsFrame> stack_;
  std::uint32_t reachable_ = 0;
};
}  // namespace

bool DominatorTree::Dominates(BlockId ancestor, BlockId descendant) const {
  if (ancestor.value >= nodes.size() || descendant.value >= nodes.size())
    return false;
  const auto& a = nodes[ancestor.value];
  const auto& b = nodes[descendant.value];
  return a.begin != 0 && b.begin != 0 && a.begin <= b.begin && b.begin < a.end;
}

DominatorResult BuildNormalDominators(const NormalFlow& graph, BlockId entry,
                                      DominatorBudget budget) {
  try {
    return DominatorBuilder(graph, entry, budget).Build();
  } catch (DominatorError error) {
    return error;
  } catch (const std::bad_alloc&) {
    return DominatorError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
