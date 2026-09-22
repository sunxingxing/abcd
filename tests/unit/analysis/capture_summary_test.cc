#include "abcd/analysis/capture_summary.h"

#include <cstdlib>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd::analysis;
  const std::vector<BoundLexicalSite> uses{{0, {CapturedDepth{3}, {2}}},
                                           {1, {EnvironmentId{0}, {2}}},
                                           {2, {CapturedDepth{0}, {1}}},
                                           {3, {CapturedDepth{3}, {2}}},
                                           {4, {CapturedDepth{3}, {1}}}};
  auto summary = std::get<CaptureSummary>(CaptureSummary::Build(uses));
  Check(summary.Uses().size() == 3 && summary.Sites() == 4);
  Check(summary.Uses()[0].captured.depth.value == 0);
  Check(summary.Find({{3}, {2}})->sites == 2);
  Check(summary.Find({{3}, {1}})->sites == 1);
  Check(!summary.Find({{3}, {0}}));
  Check(!summary.Find({{UINT32_MAX}, {UINT16_MAX}}));
  Check(std::holds_alternative<CaptureSummary>(
      CaptureSummary::Build(uses, {summary.PeakBytes()})));
  Check(std::get<CaptureSummaryError>(
            CaptureSummary::Build(uses, {summary.PeakBytes() - 1})) ==
        CaptureSummaryError::kResourceLimit);
  Check(std::get<CaptureSummaryError>(CaptureSummary::Build(uses, {0})) ==
        CaptureSummaryError::kResourceLimit);
  const std::vector<BoundLexicalSite> local{{0, {EnvironmentId{0}, {0}}}};
  Check(std::get<CaptureSummary>(CaptureSummary::Build(local, {0}))
            .Uses()
            .empty());
  Check(std::get<CaptureSummary>(CaptureSummary::Build({}, {0})).PeakBytes() ==
        0);
  auto moved = std::move(summary);
  Check(summary.Uses().empty() && summary.Sites() == 0 &&
        !summary.Find({{3}, {2}}));
  Check(moved.Find({{3}, {2}})->sites == 2);
  summary = std::move(moved);
  Check(moved.Uses().empty() && moved.Sites() == 0 &&
        moved.AllocatedBytes() == 0 && moved.PeakBytes() == 0);
  Check(summary.Find({{3}, {2}})->sites == 2);
  for (const std::uint32_t count : {200U, 201U, 4096U, 65536U}) {
    std::vector<BoundLexicalSite> sites;
    for (std::uint32_t i = 0; i < count; ++i)
      sites.push_back({i, {CapturedDepth{count - i}, {0}}});
    const auto distinct =
        std::get<CaptureSummary>(CaptureSummary::Build(sites));
    Check(distinct.Uses().size() == count && distinct.Sites() == count);
    for (std::uint32_t i = 1; i <= count; ++i)
      Check(distinct.Find({{i}, {0}})->sites == 1);
    for (auto& site : sites)
      site.binding.environment = CapturedDepth{UINT32_MAX};
    const auto repeated =
        std::get<CaptureSummary>(CaptureSummary::Build(sites));
    Check(repeated.Uses().size() == 1 && repeated.Sites() == count);
    Check(repeated.Find({{UINT32_MAX}, {0}})->sites == count);
    Check(repeated.AllocatedBytes() == sizeof(CaptureUse));
    Check(distinct.AllocatedBytes() == count * sizeof(CaptureUse));
  }
}
