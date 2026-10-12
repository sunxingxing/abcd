#include "process_limit.h"

#include <sys/resource.h>

#include <algorithm>

namespace abcd::tools {
ProcessLimit EstablishProcessLimit() {
#if defined(__APPLE__)
  return {LimitKind::kNone, std::nullopt, false};
#else
  ProcessLimit result{LimitKind::kAddressSpace, std::nullopt, false};
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) return result;
  limit.rlim_cur = std::min<rlim_t>(limit.rlim_cur, kProcessMemoryBytes);
  if (setrlimit(RLIMIT_AS, &limit) != 0) return result;
  result.bytes = static_cast<std::uint64_t>(limit.rlim_cur);
  result.established = true;
  return result;
#endif
}

std::optional<std::uint64_t> PeakResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) return {};
#if defined(__APPLE__)
  constexpr std::uint64_t kRssUnitBytes = 1;  // Darwin reports bytes.
#else
  constexpr std::uint64_t kRssUnitBytes = 1024;  // Linux reports KiB.
#endif
  return static_cast<std::uint64_t>(usage.ru_maxrss) * kRssUnitBytes;
}

bool ProcessLimitAcceptable(const ProcessLimit& limit) {
  return limit.kind == LimitKind::kNone || limit.established;
}

namespace {
nlohmann::json DescribeLimit(const ProcessLimit& limit) {
  return {{"kind", limit.kind == LimitKind::kAddressSpace ? "address_space" : "none"},
          {"bytes", limit.bytes ? nlohmann::json(*limit.bytes) : nlohmann::json(nullptr)},
          {"established", limit.established}};
}
}  // namespace

void ReportLimitFailure(std::ostream& output, const ProcessLimit& limit) {
  output << nlohmann::json{{"error", "process_limit"},
                           {"process_limit", DescribeLimit(limit)}}
         << '\n';
}

void ReportProcessMemory(nlohmann::json& report, const ProcessLimit& limit) {
  report["process_limit"] = DescribeLimit(limit);
  report["address_space_limit_bytes"] =
      limit.established ? nlohmann::json(*limit.bytes) : nlohmann::json(nullptr);
  const auto peak = PeakResidentBytes();
  report["peak_rss_bytes"] = peak ? nlohmann::json(*peak) : nlohmann::json(nullptr);
}
}  // namespace abcd::tools
