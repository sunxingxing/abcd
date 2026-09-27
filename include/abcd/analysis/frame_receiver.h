#ifndef ABCD_ANALYSIS_FRAME_RECEIVER_H_
#define ABCD_ANALYSIS_FRAME_RECEIVER_H_

#include <cstdint>

namespace abcd::analysis {
// Callable-owner premise, not an ABI-register fact. Initialized requires a
// language receiver throughout the admitted body; derived/arrow phases need
// separate proofs and must never be inferred from a present this register.
enum class FrameReceiver : std::uint8_t { kUnknown, kInitialized };
}  // namespace abcd::analysis
#endif
