#include "loop_forest.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "abcd/analysis/dominators.h"

namespace abcd::source {
namespace {
using Error = LoopError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class LoopBudget {
 public:
  explicit LoopBudget(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit); remaining_.bytes -= count * width;
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
};

class LoopBuilder {
 public:
  LoopBuilder(const analysis::ValueProgram& graph, LoopBudget& budget)
      : graph_(graph), budget_(budget) {}
  LoopForest Build() {
    Require(!graph_.blocks.empty() && graph_.blocks.size() < std::numeric_limits<std::uint32_t>::max() &&
        graph_.edges.size() < std::numeric_limits<std::uint32_t>::max(), Error::kInvalidGraph);
    Index();
    const auto allowance = budget_.Remaining();
    auto dominated = analysis::BuildNormalDominators(normal_, {}, {allowance.bytes, allowance.work});
    if (const auto* error = std::get_if<analysis::DominatorError>(&dominated)) {
      if (*error == analysis::DominatorError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == analysis::DominatorError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kInvalidGraph;
    }
    dominators_ = std::get<analysis::DominatorTree>(std::move(dominated));
    budget_.Step(dominators_.work.steps);
    budget_.Admit(dominators_.work.allocated_bytes, 1);
    const auto count = graph_.blocks.size();
    budget_.Array(order_, count);
    budget_.Array(sets_, count);
    budget_.Array(ranks_, count);
    budget_.Array(labels_, count);
    budget_.Array(result_.loops, count);
    budget_.Array(result_.owners, count);
    budget_.Array(result_.backedges, graph_.edges.size());
    budget_.Array(result_.forward_order, count);
    // Each edge can seed one backedge visit and one predecessor expansion.
    // A header expands only when its already-discovered loop is contracted.
    Require(graph_.edges.size() <= std::numeric_limits<std::size_t>::max() / 2, Error::kResourceLimit);
    budget_.Array(pending_, graph_.edges.size() * 2);
    for (std::size_t i = 0; i < count; ++i) {
      budget_.Step();
      const auto stamp = dominators_.nodes[i].begin;
      Require(stamp != 0, Error::kUnreachable);
      Require(stamp <= count, Error::kInvalidGraph);
      order_[stamp - 1] = {static_cast<std::uint32_t>(i)};
    }
    Discover();
    ForwardOrder();
    result_.loops.resize(loop_count_);
    result_.revision = graph_.revision;
    result_.cfg_revision = graph_.cfg_revision;
    result_.allocated_bytes = sizeof(LoopForest) + result_.loops.capacity() * sizeof(NaturalLoop) +
        result_.owners.capacity() * sizeof(std::optional<LoopId>) +
        result_.backedges.capacity() * sizeof(std::optional<LoopId>) +
        result_.forward_order.capacity() * sizeof(analysis::WorkBlockId);
    result_.work = budget_.Work();
    return std::move(result_);
  }

 private:
  void Index() {
    const auto count = graph_.blocks.size();
    budget_.Array(normal_.blocks, count);
    budget_.Array(offsets_, count + 1);
    budget_.Array(predecessors_, graph_.edges.size());
    budget_.Array(indegree_, count);
    std::size_t next = 0;
    for (std::size_t b = 0; b < count; ++b) {
      budget_.Step();
      const auto& block = graph_.blocks[b];
      Require(block.first_edge.value == next && block.edge_count <= analysis::kMaxNormalSuccessors &&
          block.edge_count <= graph_.edges.size() - next, Error::kInvalidGraph);
      auto& adapted = normal_.blocks[b];
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        budget_.Step();
        const auto& edge = graph_.edges[next++];
        Require(edge.source.value == b && edge.successor == e && edge.target.value < count &&
            (edge.kind == analysis::FlowEdgeKind::kTaken || edge.kind == analysis::FlowEdgeKind::kFallthrough),
            Error::kInvalidGraph);
        adapted.successors[adapted.successor_count++] = {{edge.target.value}, edge.kind};
        ++indegree_[edge.target.value];
      }
    }
    Require(next == graph_.edges.size(), Error::kInvalidGraph);
    for (std::size_t b = 0; b < count; ++b) {
      budget_.Step(); offsets_[b + 1] = offsets_[b] + indegree_[b]; indegree_[b] = 0;
    }
    for (std::size_t e = 0; e < graph_.edges.size(); ++e) {
      budget_.Step();
      const auto target = graph_.edges[e].target.value;
      predecessors_[offsets_[target] + indegree_[target]++] = {static_cast<std::uint32_t>(e)};
    }
  }
  bool Dominates(analysis::WorkBlockId header, analysis::WorkBlockId block) const {
    return dominators_.Dominates({header.value}, {block.value});
  }
  LoopId Root(LoopId id) {
    auto root = id;
    while (sets_[root.value] != root) { budget_.Step(); root = sets_[root.value]; }
    while (sets_[id.value] != id) {
      budget_.Step(); const auto next = sets_[id.value]; sets_[id.value] = root; id = next;
    }
    return root;
  }
  void Attach(LoopId child, LoopId parent) {
    Require(!result_.loops[child.value].parent, Error::kIrreducible);
    result_.loops[child.value].parent = parent;
    auto a = Root(child);
    auto b = Root(parent);
    Require(a != b, Error::kIrreducible);
    // Union rank bounds depth; the label preserves the semantic outer loop
    // independently of which union-find root wins. Do not walk loop parents.
    if (ranks_[a.value] < ranks_[b.value]) std::swap(a, b);
    sets_[b.value] = a;
    if (ranks_[a.value] == ranks_[b.value]) ++ranks_[a.value];
    labels_[a.value] = parent;
  }
  void Push(analysis::WorkBlockId block) {
    Require(pending_count_ < pending_.size(), Error::kResourceLimit);
    pending_[pending_count_++] = block;
  }
  void Predecessors(analysis::WorkBlockId block) {
    for (auto p = offsets_[block.value]; p < offsets_[block.value + 1]; ++p) {
      budget_.Step(); Push(graph_.edges[predecessors_[p].value].source);
    }
  }
  void Discover() {
    // Dominator reverse preorder processes nested headers before ancestors.
    // See loop-discovery.md for provenance and the compact ownership invariant.
    for (std::size_t ordinal = order_.size(); ordinal != 0; --ordinal) {
      budget_.Step();
      const auto header = order_[ordinal - 1];
      std::optional<LoopId> loop;
      for (auto p = offsets_[header.value]; p < offsets_[header.value + 1]; ++p) {
        budget_.Step();
        const auto edge = predecessors_[p];
        const auto source = graph_.edges[edge.value].source;
        if (!Dominates(header, source)) continue;
        if (!loop) {
          loop = LoopId{loop_count_++};
          result_.loops[loop->value] = {header, {}};
          sets_[loop->value] = *loop; labels_[loop->value] = *loop;
          Require(!result_.owners[header.value], Error::kIrreducible);
          result_.owners[header.value] = *loop;
        }
        result_.backedges[edge.value] = *loop;
        Push(source);
      }
      if (!loop) continue;
      while (pending_count_ != 0) {
        budget_.Step();
        const auto block = pending_[--pending_count_];
        Require(Dominates(header, block), Error::kIrreducible);
        const auto owner = result_.owners[block.value];
        if (!owner) {
          result_.owners[block.value] = *loop;
          Predecessors(block);
        } else {
          const auto outer = labels_[Root(*owner).value];
          if (outer == *loop) continue;
          Attach(outer, *loop);
          Predecessors(result_.loops[outer.value].header);
        }
      }
    }
  }
  void ForwardOrder() {
    // Removing all and only dominance backedges must leave a DAG. Remaining
    // cycles are irreducible and cannot be given invented lexical loop owners.
    for (std::size_t e = 0; e < graph_.edges.size(); ++e) {
      budget_.Step(); if (result_.backedges[e]) --indegree_[graph_.edges[e].target.value];
    }
    std::size_t ready = 0;
    for (std::size_t b = 0; b < graph_.blocks.size(); ++b) {
      budget_.Step(); if (indegree_[b] == 0) result_.forward_order[ready++] = {static_cast<std::uint32_t>(b)};
    }
    for (std::size_t i = 0; i < ready; ++i) {
      budget_.Step();
      const auto& block = graph_.blocks[result_.forward_order[i].value];
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        budget_.Step();
        const auto id = block.first_edge.value + e;
        if (result_.backedges[id]) continue;
        const auto target = graph_.edges[id].target;
        if (--indegree_[target.value] == 0) result_.forward_order[ready++] = target;
      }
    }
    Require(ready == graph_.blocks.size(), Error::kIrreducible);
  }
  const analysis::ValueProgram& graph_;
  LoopBudget& budget_;
  LoopForest result_;
  analysis::NormalFlow normal_;
  analysis::DominatorTree dominators_;
  std::vector<std::uint32_t> offsets_, indegree_, ranks_;
  std::vector<analysis::EdgeId> predecessors_;
  std::vector<analysis::WorkBlockId> order_, pending_;
  std::vector<LoopId> sets_, labels_;
  std::uint32_t loop_count_ = 0;
  std::size_t pending_count_ = 0;
};
}  // namespace

std::variant<LoopForest, LoopError> BuildLoopForest(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  try {
    LoopBudget quota(budget);
    quota.Admit(1, sizeof(LoopBuilder));
    return LoopBuilder(graph, quota).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
