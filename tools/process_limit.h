#ifndef ABCD_TOOLS_PROCESS_LIMIT_H_
#define ABCD_TOOLS_PROCESS_LIMIT_H_

#include <cstdint>
#include <optional>
#include <ostream>

#include <nlohmann/json.hpp>

namespace abcd::tools {
// Process memory boundary shared by every CLI (AGENTS.md §2: the whole
// process must stay within 1,000,000,000 bytes).
// Linux: the RLIMIT_AS soft limit is lowered to this value. It bounds the
// address space, not RSS, and an inherited lower limit is never raised.
// Darwin: XNU checks RLIMIT_AS against the existing VM size, so it is neither
// settable here nor a portable RSS bound. The limit is reported as not
// established instead of being claimed. Peak RSS measures the run only; it is
// not a guarantee. Whole-process admission/budgets remain a separate gate.
inline constexpr std::uint64_t kProcessMemoryBytes = 1000000000;

enum class LimitKind : std::uint8_t { kAddressSpace, kNone };
struct ProcessLimit {
  LimitKind kind = LimitKind::kNone;
  // Effective soft limit after establishment; absent when none applies.
  std::optional<std::uint64_t> bytes;
  bool established = false;
};

// Linux failure to lower the limit returns established=false with kind
// kAddressSpace; callers treat that as fatal. Darwin returns kind kNone.
[[nodiscard]] ProcessLimit EstablishProcessLimit();
// Peak resident set of this process in bytes, with the platform unit applied
// (Linux reports KiB, Darwin bytes). Absent if the host cannot report it.
[[nodiscard]] std::optional<std::uint64_t> PeakResidentBytes();
// Required on hosts that support the limit; Darwin runs unbounded and says so.
[[nodiscard]] bool ProcessLimitAcceptable(const ProcessLimit& limit);
// Report fields: process_limit {kind, bytes, established},
// address_space_limit_bytes (null when not established), peak_rss_bytes.
void ReportProcessMemory(nlohmann::json& report, const ProcessLimit& limit);
// Real allocation failures are observed out of band: a std::new_handler sets
// a flag before throwing std::bad_alloc, so a failure stays visible even when
// a lower stage folds the exception into its own resource-limit error. Budget
// exhaustion never reaches operator new failure and does not set the flag.
void InstallAllocationFailureMonitor();
// Sticky process-level signal: true once any allocation has failed since the
// monitor was installed. It is never cleared, so a failure that a lower stage
// folded or recovered from still stops the run.
[[nodiscard]] bool AllocationFailureObserved();
// Structured diagnostic for a limit that could not be established; written as
// one JSON line {"error":"process_limit","process_limit":{...}}.
void ReportLimitFailure(std::ostream& output, const ProcessLimit& limit);
}  // namespace abcd::tools

#endif  // ABCD_TOOLS_PROCESS_LIMIT_H_
