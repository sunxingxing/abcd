#include "abcd/analysis/capture_summary.h"

#include <algorithm>
#include <new>
#include <utility>

namespace abcd::analysis {
namespace {
auto Key(CapturedSlot captured) {
  return std::pair{captured.depth.value, captured.slot.value};
}
}  // namespace

std::variant<CaptureSummary, CaptureSummaryError> CaptureSummary::Build(
    std::span<const BoundLexicalSite> bindings, CaptureSummaryBudget budget) {
  std::size_t count = 0;
  for (const auto& site : bindings)
    if (std::holds_alternative<CapturedDepth>(site.binding.environment)) {
      if (count == budget.bytes / sizeof(CapturedSlot))
        return CaptureSummaryError::kResourceLimit;
      ++count;
    }
  CaptureSummary result;
  if (count == 0) return result;
  try {
    auto scratch = std::make_unique<CapturedSlot[]>(count);
    std::size_t at = 0;
    for (const auto& site : bindings)
      if (const auto* capture =
              std::get_if<CapturedDepth>(&site.binding.environment))
        scratch[at++] = {*capture, site.binding.slot};
    std::sort(scratch.get(), scratch.get() + count,
              [](CapturedSlot left, CapturedSlot right) {
                return Key(left) < Key(right);
              });
    std::size_t unique = 1;
    for (std::size_t i = 1; i < count; ++i)
      if (Key(scratch[i - 1]) != Key(scratch[i])) ++unique;
    const auto scratch_bytes = count * sizeof(CapturedSlot);
    if (unique > (budget.bytes - scratch_bytes) / sizeof(CaptureUse))
      return CaptureSummaryError::kResourceLimit;
    // Admit the overlap, then retain only unique requirements. A highly
    // repeated slot must not pin a use-count-sized cache allocation.
    result.uses_ = std::make_unique<CaptureUse[]>(unique);
    result.size_ = unique;
    result.sites_ = count;
    result.peak_ = scratch_bytes + unique * sizeof(CaptureUse);
    at = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if (i == 0 || Key(scratch[i - 1]) != Key(scratch[i]))
        result.uses_[at++] = {scratch[i], 1};
      else
        ++result.uses_[at - 1].sites;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return CaptureSummaryError::kResourceLimit;
  }
}

const CaptureUse* CaptureSummary::Find(CapturedSlot captured) const {
  if (!uses_) return nullptr;
  const auto* begin = uses_.get();
  const auto* end = begin + size_;
  const auto* found = std::lower_bound(
      begin, end, captured, [](const CaptureUse& use, CapturedSlot key) {
        return Key(use.captured) < Key(key);
      });
  return found != end && Key(found->captured) == Key(captured) ? found
                                                               : nullptr;
}
}  // namespace abcd::analysis
