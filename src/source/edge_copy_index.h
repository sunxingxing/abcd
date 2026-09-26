#ifndef ABCD_SOURCE_EDGE_COPY_INDEX_H_
#define ABCD_SOURCE_EDGE_COPY_INDEX_H_

#include <algorithm>
#include <limits>
#include <type_traits>

#include "edge_copies.h"

namespace abcd::source {
inline void RequireCopy(bool condition, CopyError error) { if (!condition) throw error; }
class CopyBudget {
 public:
  explicit CopyBudget(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    RequireCopy(count <= remaining_.work, CopyError::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    RequireCopy(count <= remaining_.bytes / width, CopyError::kResourceLimit); remaining_.bytes -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& items, std::size_t count) {
    Admit(count, sizeof(T)); items.reserve(count); Admit(items.capacity() - count, sizeof(T));
  }
  template <class T> void Array(std::vector<T>& items, std::size_t count) {
    Reserve(items, count); Step(count); items.resize(count);
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
};
struct CopyDestination { analysis::ValueId value; std::uint32_t row; };
class CopyIndex {
 public:
  CopyIndex(std::span<const ParallelCopy> copies, CopyBudget& budget) : budget_(budget) {
    RequireCopy(copies.size() <= std::numeric_limits<std::uint32_t>::max(), CopyError::kResourceLimit);
    budget.Reserve(items_, copies.size());
    for (std::size_t i = 0; i < copies.size(); ++i) {
      budget.Step(); items_.push_back({copies[i].destination, static_cast<std::uint32_t>(i)});
    }
    std::sort(items_.begin(), items_.end(), [&](auto a, auto b) { budget.Step(); return a.value < b.value; });
    for (std::size_t i = 1; i < items_.size(); ++i) {
      budget.Step(); RequireCopy(items_[i - 1].value != items_[i].value, CopyError::kDuplicateDestination);
    }
  }
  std::optional<std::uint32_t> Find(analysis::ValueId value) const {
    const auto found = std::lower_bound(items_.begin(), items_.end(), value,
        [&](auto item, auto key) { budget_.Step(); return item.value < key; });
    if (found == items_.end() || found->value != value) return {};
    return found->row;
  }
 private:
  CopyBudget& budget_;
  std::vector<CopyDestination> items_;
};
inline bool SameCopyValue(const analysis::ValueOperand& a, const analysis::ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::SpecialValue>) return value == std::get<T>(b);
    else return value.value == std::get<T>(b).value;
  }, a);
}
}  // namespace abcd::source
#endif
