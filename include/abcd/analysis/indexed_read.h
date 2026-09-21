#ifndef ABCD_ANALYSIS_INDEXED_READ_H_
#define ABCD_ANALYSIS_INDEXED_READ_H_

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
struct PropertyIndex {
  std::uint32_t value;
};
struct IndexedRead {
  ScalarSlot receiver;
  PropertyIndex index;
};
enum class IndexedReadError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// Models the pinned runtime's uint32-index operation, NOT generic JS bracket
// access. Its proxy/dictionary paths need runtime-specific proof before source
// lowering; do not convert this to PropertyRead solely from the opcode name.
// Reads the receiver, may invoke user code/throw, writes acc only on success.
// Source registers are preserved. DecodeInstruction input required; constant
// time/storage, no runtime evaluation or allocation. See property-indices.md.
[[nodiscard]] std::variant<IndexedRead, IndexedReadError> LiftIndexedRead(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_INDEXED_READ_H_
