#ifndef ABCD_ANALYSIS_CALL_RESULT_H_
#define ABCD_ANALYSIS_CALL_RESULT_H_

#include <cstdint>

namespace abcd::analysis {
// Execution-contract premise, not callee inference. Applies only to normal
// call results; callers still validate every input and explicit ABC return.
enum class CallResultValues : std::uint8_t { kUnknown, kLanguage };
}  // namespace abcd::analysis
#endif
