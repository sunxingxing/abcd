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
  explicit PhiQuota(SemanticBudget budget) : initial_(budget), remaining_(budget) {}
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
  std::size_t UsedBytes() const { return initial_.bytes - remaining_.bytes; }
 private:
  SemanticBudget initial_, remaining_;
};
struct IdentityNode {
  std::optional<std::uint32_t> index;
  std::uint32_t low = 0;
  bool active = false;
  std::optional<ValueId> component;  // Root value gives a stable component ID.
};
struct IdentityFrame {
  ValueId value;
  std::optional<UseId> next;
};

enum class IdentityDomain : std::uint8_t { kPhis, kCopiesAndPhis };
class ValueIdentityIndex {
 public:
  ValueIdentityIndex(const ValueProgram& graph, PhiQuota& quota, IdentityDomain domain)
      : graph_(graph), domain_(domain), quota_(quota) {}
  ValueIdentities Run() {
    const auto sized = [&](auto& items, std::size_t size) {
      quota_.Reserve(items, size);
      quota_.Work(size);
      items.resize(size);
    };
    const auto count = graph_.values.size();
    sized(nodes_, count);
    sized(equations_, count);
    sized(canonical_, count);
    sized(head_, count);
    sized(next_, graph_.uses.size());
    quota_.Reserve(frames_, count);
    quota_.Reserve(active_, count);
    quota_.Reserve(members_, count);
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work();
      canonical_[i] = ValueId{static_cast<std::uint32_t>(i)};
      const auto& value = graph_.values[i];
      if (value.life != ValueLife::kLive) continue;
      equations_[i] = std::holds_alternative<PhiDefinition>(value.origin);
      if (domain_ == IdentityDomain::kPhis) continue;
      const auto* origin = std::get_if<OpResult>(&value.origin);
      if (!origin) continue;
      const auto& op = graph_.ops[origin->op.value];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(op.operation) ||
          !CanErasePureOp(op) || op.result != ValueId{static_cast<std::uint32_t>(i)} || op.use_count != 1) continue;
      const auto& use = graph_.uses[op.first_use.value];
      const auto* source = std::get_if<ValueId>(&use.operand);
      if (source && use.role == OperandRole::kInput && use.purpose == ReadPurpose::kOperand)
        equations_[i] = 1;
    }
    for (std::size_t i = 0; i < graph_.uses.size(); ++i) {
      quota_.Work();
      const auto& use = graph_.uses[i];
      if (use.life != ValueLife::kLive) continue;
      std::optional<ValueId> owner;
      if (const auto* phi = std::get_if<ValueId>(&use.owner)) owner = *phi;
      else if (const auto* op = std::get_if<OpId>(&use.owner)) owner = graph_.ops[op->value].result;
      if (!owner || !equations_[owner->value]) continue;
      next_[i] = head_[owner->value];
      head_[owner->value] = UseId{static_cast<std::uint32_t>(i)};
    }
    // Iterative Tarjan over actual phi/copy equations and their input identities.
    // SCCs finish in dependency order: no repeated global phi-input rescans.
    // See docs/design/algorithms.md; each vertex/edge is visited O(1) times.
    for (std::size_t i = 0; i < count; ++i) {
      quota_.Work();
      const ValueId value{static_cast<std::uint32_t>(i)};
      if (!IsEquation(value) || nodes_[i].index) continue;
      Discover(value);
      while (!frames_.empty()) {
        quota_.Work();
        auto& frame = frames_.back();
        const auto current = frame.value;
        auto& node = nodes_[current.value];
        if (frame.next) {
          const auto use = *frame.next;
          frame.next = next_[use.value];
          const auto* input = std::get_if<ValueId>(&graph_.uses[use.value].operand);
          if (!input) continue;
          const auto target = *input;
          if (!IsEquation(target)) continue;
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
    return {std::move(canonical_), stats_.collapsed_values, quota_.UsedBytes(), 0};
  }

 private:
  bool IsEquation(ValueId value) const { return equations_[value.value] != 0; }
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
        const auto* operand = std::get_if<ValueId>(&graph_.uses[use->value].operand);
        if (!operand) { unique = false; continue; }
        const auto input = *operand;
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
  const ValueProgram& graph_;
  IdentityDomain domain_;
  std::vector<std::uint8_t> equations_;
  PhiQuota& quota_;
  PhiSimplifyStats stats_;
  std::uint32_t next_index_ = 0;
  std::vector<IdentityNode> nodes_;
  std::vector<ValueId> canonical_;
  std::vector<std::optional<UseId>> head_, next_;
  std::vector<IdentityFrame> frames_;
  std::vector<ValueId> active_, members_;
};
}  // namespace

std::variant<ValueIdentities, ValueSimplifyError> AnalyzeValueIdentities(
    const ValueProgram& graph, SemanticBudget budget) {
  try {
    PhiQuota quota{budget};
    auto result = ValueIdentityIndex(graph, quota, IdentityDomain::kCopiesAndPhis).Run();
    result.work = budget.work - quota.RemainingWork();
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<PhiSimplifyStats, ValueSimplifyError> SimplifyPhis(
    ValueProgram& candidate, SemanticBudget budget,
    std::span<ValueId> representatives) {
  try {
    PhiQuota quota{budget};
    if (!representatives.empty() && representatives.size() != candidate.values.size())
      throw Error::kInvalidGraph;
    const auto identities = ValueIdentityIndex(candidate, quota, IdentityDomain::kPhis).Run();
    PhiSimplifyStats result; result.collapsed_values = identities.collapsed_values;
    for (std::size_t i = 0; i < candidate.uses.size(); ++i) {
      quota.Work(); const auto& use = candidate.uses[i];
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (use.life != ValueLife::kLive || !value || identities.canonical[value->value] == *value) continue;
      if (ReplaceUse(candidate, UseId{static_cast<std::uint32_t>(i)}, identities.canonical[value->value]))
        throw Error::kInvalidGraph;
      ++result.replaced_uses;
    }
    for (auto& value : representatives) {
      quota.Work(); if (value.value >= identities.canonical.size()) throw Error::kInvalidGraph;
      value = identities.canonical[value.value];
    }
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
