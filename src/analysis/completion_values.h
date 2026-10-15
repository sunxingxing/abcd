#ifndef ABCD_ANALYSIS_COMPLETION_VALUES_H_
#define ABCD_ANALYSIS_COMPLETION_VALUES_H_
#include "abcd/analysis/completion_relation.h"
#include <map>

namespace abcd::analysis::detail {
enum class CompletionConstant : std::uint8_t { kUnknown, kHole, kUndefined, kNull, kFalse, kTrue, kInteger };
struct CompletionValue {
  CompletionPayload payload;
  CompletionConstant constant = CompletionConstant::kUnknown;
  std::int32_t integer = 0;
  auto operator<=>(const CompletionValue&) const = default;
};
template<class T> struct CompletionLess {
  RawEventArena* arena;
  bool operator()(const T& a, const T& b) const { arena->Step(); return a < b; }
};
// Persistent fixed-width SSA map. Every update copies at most 33 trie nodes;
// ordered interning canonicalizes equal environments, including loop fixed
// points. The 32 levels come from SsaValueId's specified width, not graph shape.
// All nodes, interning entries and overlapping vector growth use the prepaid
// arena. IDs/values are never addresses into a reallocating vector.
template<class Value = CompletionValue>
class BasicCompletionValues {
  struct Node {
    std::uint32_t left = 0, right = 0, atom = 0;
    auto operator<=>(const Node&) const = default;
  };
 public:
  explicit BasicCompletionValues(RawEventArena& arena)
      : arena_(arena), nodes_(arena.Resource()), values_(arena.Resource()),
        nodes_index_(CompletionLess<Node>{&arena},arena.Resource()),
        values_index_(CompletionLess<Value>{&arena},arena.Resource()) {
    nodes_.push_back({}); values_.push_back({});
  }
  std::optional<Value> Get(std::uint32_t root, SsaValueId key) const {
    for (unsigned bit = kBits; root && bit; --bit) {
      arena_.Step(); const auto n = nodes_[root];
      root = (key.value >> (bit - 1)) & 1 ? n.right : n.left;
    }
    if (!root) return {};
    return values_[nodes_[root].atom];
  }
  std::uint32_t Set(std::uint32_t root, SsaValueId key, const Value& value) {
    const auto found = Get(root,key); if (found && *found == value) return root;
    std::array<std::uint32_t,kBits> path{};
    auto cursor=root;
    for (unsigned bit=kBits;bit;--bit) {
      arena_.Step(); path[kBits-bit]=cursor;
      if (cursor) cursor=(key.value>>(bit-1))&1 ? nodes_[cursor].right : nodes_[cursor].left;
    }
    auto at=values_index_.find(value);
    std::uint32_t atom;
    if (at!=values_index_.end()) atom=at->second;
    else {
      Size(values_.size()); atom=static_cast<std::uint32_t>(values_.size());
      values_.push_back(value); values_index_.emplace(value,atom);
    }
    cursor=Intern({0,0,atom});
    for (unsigned bit=0;bit<kBits;++bit) {
      auto n=nodes_[path[kBits-1-bit]];
      if ((key.value>>bit)&1) n.right=cursor; else n.left=cursor;
      cursor=Intern(n);
    }
    return cursor;
  }
 private:
  static constexpr unsigned kBits = std::numeric_limits<std::uint32_t>::digits;
  static void Size(std::size_t size) {
    if (size>=UINT32_MAX) throw CompletionRelationFailure{CompletionRelationError::kResourceLimit};
  }
  std::uint32_t Intern(Node node) {
    arena_.Step(); const auto at=nodes_index_.find(node);
    if (at!=nodes_index_.end()) return at->second;
    Size(nodes_.size()); const auto id=static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(node); nodes_index_.emplace(node,id); return id;
  }
  RawEventArena& arena_;
  std::pmr::vector<Node> nodes_;
  std::pmr::vector<Value> values_;
  std::pmr::map<Node,std::uint32_t,CompletionLess<Node>> nodes_index_;
  std::pmr::map<Value,std::uint32_t,CompletionLess<Value>> values_index_;
};
using CompletionValues = BasicCompletionValues<>;
}  // namespace abcd::analysis::detail
#endif
