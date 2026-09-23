#ifndef ABCD_ANALYSIS_INDEXED_WRITE_H_
#define ABCD_ANALYSIS_INDEXED_WRITE_H_

#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct IndexedWrite {
  abc::RegisterId object;
  std::uint32_t index = 0;
};
enum class IndexedWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// Pinned v7 STOWNBYINDEX reads acc and defines a writable, enumerable,
// configurable own property at the encoded index; acc is preserved.
[[nodiscard]] std::variant<IndexedWrite, IndexedWriteError> LiftIndexedWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_INDEXED_WRITE_H_
