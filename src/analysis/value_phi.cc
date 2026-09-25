#include "value_phi.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "value_edits.h"

namespace abcd::analysis {
namespace {
using Error = ValueSimplifyError;
class PhiQuota {
 public:
  explicit PhiQuota(SemanticBudget budget) : remaining_(budget) {}
  void Work(std::size_t count = 1) {
    if (count > remaining_.work) throw Error::kWorkLimit;
    remaining_.work -= count;
  }
  template <typename T> void Reserve(std::vector<T>& items, std::size_t count) {
    if (count > std::numeric_limits<std::uint32_t>::max() ||
        count > remaining_.bytes / sizeof(T)) throw Error::kResourceLimit;
    remaining_.bytes -= count * sizeof(T);
    items.reserve(count);
    const auto extra = items.capacity() - count;
    if (extra > remaining_.bytes / sizeof(T)) throw Error::kResourceLimit;
    remaining_.bytes -= extra * sizeof(T);
  }
  std::uint64_t RemainingWork() const { return remaining_.work; }
 private:
  SemanticBudget remaining_;
};
struct PhiNode {
  std::optional<std::uint32_t> index;
  std::uint32_t low = 0;
  bool active = false;
  std::optional<ValueId> component;  // Root value gives a stable component ID.
};
struct PhiFrame {
  ValueId value;
  std::optional<UseId> next;
};

class PhiCollapse {
 public:
  PhiCollapse(ValueProgram& graph, PhiQuota& quota) : graph_(graph), quota_(quota) {}
  PhiSimplifyStats Run() {
    const auto sized = [&](auto& items, std::size_t size) {
      quota_.Reserve(items, size);
      quota_.Work(size);
      items.resize(size);
    };
    const auto count = graph_.values.size();
    sized(nodes_, count);
    sized(canonical_, count);
    sized(head_, count);
    sized(next_, graph_.uses.size());
    quota_.Reserve(frames_, count);
    quota_.Reserve(active_, count);
    quota_.Reserve(members_, count);
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work();
      canonical_[i] = ValueId{static_cast<std::uint32_t>(i)};
    }
    for (std::size_t i = 0; i < graph_.uses.size(); ++i) {
      quota_.Work();
      const auto& use = graph_.uses[i];
      const auto* phi = std::get_if<ValueId>(&use.owner);
      if (!phi || use.life != ValueLife::kLive) continue;
      next_[i] = head_[phi->value];
      head_[phi->value] = UseId{static_cast<std::uint32_t>(i)};
    }
    // Iterative Tarjan, dependency edges point from a phi to its input phis.
    // SCCs finish in dependency order: no repeated global phi-input rescans.
    // See docs/design/algorithms.md; each vertex/edge is visited O(1) times.
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work();
      const ValueId value{static_cast<std::uint32_t>(i)};
      if (!IsPhi(value) || nodes_[i].index) continue;
      Discover(value);
      while (!frames_.empty()) {
        quota_.Work();
        auto& frame = frames_.back();
        const auto current = frame.value;
        auto& node = nodes_[current.value];
        if (frame.next) {
          const auto use = *frame.next;
          frame.next = next_[use.value];
          const auto target = std::get<ValueId>(graph_.uses[use.value].operand);
          if (!IsPhi(target)) continue;
          const auto& dependency = nodes_[target.value];
          if (!dependency.index) Discover(target);
          else if (dependency.active) node.low = std::min(node.low, *dependency.index);
          continue;
        }
        frames_.pop_back();
        if (node.low == *node.index) Collapse(current);
        if (!frames_.empty()) {
          auto& parent = nodes_[frames_.back().value.value];
          parent.low = std::min(parent.low, node.low);
        }
      }
    }
    for (std::size_t i = 0; i < graph_.uses.size(); ++i) {
      quota_.Work();
      const auto& use = graph_.uses[i];
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (use.life != ValueLife::kLive || !value || canonical_[value->value] == *value)
        continue;
      if (ReplaceUse(graph_, UseId{static_cast<std::uint32_t>(i)}, canonical_[value->value]))
        throw Error::kInvalidGraph;
      ++stats_.replaced_uses;
    }
    return stats_;
  }

 private:
  bool IsPhi(ValueId value) const {
    const auto& definition = graph_.values[value.value];
    return definition.life == ValueLife::kLive &&
           std::holds_alternative<PhiDefinition>(definition.origin);
  }
  void Discover(ValueId value) {
    auto& node = nodes_[value.value];
    node.index = next_index_++;
    node.low = *node.index;
    node.active = true;
    active_.push_back(value);
    frames_.push_back({value, head_[value.value]});
  }
  void Collapse(ValueId root) {
    members_.clear();
    do {
      quota_.Work();
      const auto value = active_.back();
      active_.pop_back();
      nodes_[value.value].active = false;
      nodes_[value.value].component = root;
      members_.push_back(value);
    } while (members_.back() != root);
    std::optional<ValueId> external;
    bool unique = true;
    for (const auto member : members_) {
      quota_.Work();
      for (auto use = head_[member.value]; use; use = next_[use->value]) {
        quota_.Work();
        const auto input = std::get<ValueId>(graph_.uses[use->value].operand);
        if (nodes_[input.value].component == root) continue;
        const auto value = canonical_[input.value];
        if (external && *external != value) unique = false;
        else external = value;
      }
    }
    if (!unique || !external) return;
    const auto domain = graph_.values[external->value].domain;
    if (domain.IsEmpty()) return;
    for (const auto member : members_) {
      quota_.Work();
      const auto admitted = graph_.values[member.value].domain;
      if (domain.Join(admitted) != admitted) return;
    }
    for (const auto member : members_) {
      quota_.Work();
      canonical_[member.value] = *external;
      ++stats_.collapsed_values;
    }
  }
  ValueProgram& graph_;
  PhiQuota& quota_;
  PhiSimplifyStats stats_;
  std::uint32_t next_index_ = 0;
  std::vector<PhiNode> nodes_;
  std::vector<ValueId> canonical_;
  std::vector<std::optional<UseId>> head_, next_;
  std::vector<PhiFrame> frames_;
  std::vector<ValueId> active_, members_;
};
}  // namespace

std::variant<PhiSimplifyStats, ValueSimplifyError> SimplifyPhis(
    ValueProgram& candidate, SemanticBudget budget) {
  try {
    PhiQuota quota{budget};
    auto result = PhiCollapse(candidate, quota).Run();
    result.work = budget.work - quota.RemainingWork();
    return result;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
