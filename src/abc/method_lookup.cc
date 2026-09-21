#include "abcd/abc/method_lookup.h"

#include <algorithm>
#include <new>

namespace abcd::abc {
std::variant<MethodLookup, MethodLookupError> MethodLookup::Build(
    std::span<const MethodInfo> methods, MethodLookupBudget budget) {
  if (methods.size() > UINT32_MAX ||
      methods.size() > budget.bytes / sizeof(Entry))
    return MethodLookupError::kResourceLimit;
  MethodLookup result;
  if (methods.empty()) return result;
  try {
    result.entries_ = std::make_unique<Entry[]>(methods.size());
    result.size_ = methods.size();
    for (std::size_t i = 0; i < methods.size(); ++i)
      result.entries_[i] = {methods[i].offset, {static_cast<std::uint32_t>(i)}};
    auto* begin = result.entries_.get();
    auto* end = begin + result.size_;
    std::sort(begin, end, [](const Entry& left, const Entry& right) {
      return left.offset < right.offset;
    });
    for (std::size_t i = 1; i < result.size_; ++i)
      if (begin[i - 1].offset == begin[i].offset)
        return MethodLookupError::kDuplicateOffset;
    return result;
  } catch (const std::bad_alloc&) {
    return MethodLookupError::kResourceLimit;
  }
}

std::optional<MethodPosition> MethodLookup::Find(EntityOffset offset) const {
  if (!entries_) return {};
  const auto* begin = entries_.get();
  const auto* end = begin + size_;
  const auto* found = std::lower_bound(
      begin, end, offset,
      [](const Entry& entry, EntityOffset key) { return entry.offset < key; });
  if (found == end || found->offset != offset) return {};
  return found->position;
}
}  // namespace abcd::abc
