#ifndef ABCD_ANALYSIS_CAPTURE_SUMMARY_H_
#define ABCD_ANALYSIS_CAPTURE_SUMMARY_H_

#include "abcd/analysis/creation_binding.h"

namespace abcd::analysis {
struct CaptureUse {
  CapturedSlot captured;
  std::uint64_t sites = 0;
};
enum class CaptureSummaryError { kResourceLimit };
struct CaptureSummaryBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// One validated method's captured lexical accesses, sorted by depth then slot.
// Local bindings are excluded. Deduplication shares analysis requirements, NOT
// runtime cells or creation contexts. Site counts combine reads and writes;
// this summary does not prove initialization, effects, or closure equivalence.
class CaptureSummary {
 public:
  CaptureSummary(CaptureSummary&&) noexcept = default;
  CaptureSummary& operator=(CaptureSummary&&) noexcept = default;
  [[nodiscard]] static std::variant<CaptureSummary, CaptureSummaryError> Build(
      std::span<const BoundLexicalSite> bindings,
      CaptureSummaryBudget budget = {});
  [[nodiscard]] std::span<const CaptureUse> Uses() const {
    return {uses_.get(), uses_ ? size_ : 0};
  }
  [[nodiscard]] const CaptureUse* Find(CapturedSlot captured) const;
  [[nodiscard]] std::uint64_t Sites() const { return uses_ ? sites_ : 0; }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return Uses().size() * sizeof(CaptureUse);
  }
  // Simultaneous sorting scratch + exact-size result payload, not process RSS.
  [[nodiscard]] std::size_t PeakBytes() const { return uses_ ? peak_ : 0; }

 private:
  CaptureSummary() = default;
  std::unique_ptr<CaptureUse[]> uses_;
  std::size_t size_ = 0;
  std::uint64_t sites_ = 0;
  std::size_t peak_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CAPTURE_SUMMARY_H_
