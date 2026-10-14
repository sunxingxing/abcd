#ifndef ABCD_ANALYSIS_INDEXED_WRITE_H_
#define ABCD_ANALYSIS_INDEXED_WRITE_H_

#include <variant>
#include <optional>

#include "abcd/abc/instruction.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
enum class IndexedDefinition : std::uint8_t { kOwnStore, kDefineField };
struct IndexedWrite {
  abc::RegisterId object;
  std::uint32_t index = 0;
  std::optional<std::uint16_t> cache_slot;
  IndexedDefinition kind = IndexedDefinition::kOwnStore;
};
enum class IndexedWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// Pinned v7 STOWNBYINDEX reads acc and defines a writable/configurable own
// property; class constructors/prototypes use non-enumerable descriptors.
// Acc is preserved, including the exceptional register snapshot. Wide encoding
// has no cache operand; an absent slot must not be confused with encoded zero.
// DEFINEFIELDBYINDEX instead uses CreateDataPropertyOrThrow (all flags true).
// Preserve that distinction: its source projection requires a private object
// construction, never an assignment or an array's own-store proof.
[[nodiscard]] std::variant<IndexedWrite, IndexedWriteError> LiftIndexedWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_INDEXED_WRITE_H_
