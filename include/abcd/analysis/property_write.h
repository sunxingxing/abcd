#ifndef ABCD_ANALYSIS_PROPERTY_WRITE_H_
#define ABCD_ANALYSIS_PROPERTY_WRITE_H_

#include "abcd/analysis/own_write.h"

namespace abcd::analysis {
enum class PropertyWriteKind { kAssignment, kOwnStore, kDefineField };
struct PropertyWrite {
  PropertyWriteKind kind;
  abc::PoolIndex name;
  abc::RegisterId object;
};
enum class PropertyWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
// Common indexing boundary, not a semantic equivalence: ordinary assignments
// may invoke inherited setters. Never lower all three kinds to field
// definitions.
[[nodiscard]] std::variant<PropertyWrite, PropertyWriteError> LiftPropertyWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif
