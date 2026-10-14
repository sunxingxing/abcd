#ifndef ABCD_ANALYSIS_ITERATOR_H_
#define ABCD_ANALYSIS_ITERATOR_H_

#include <cstdint>
#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct PropertyIterator {};
struct NextPropertyName {
  abc::RegisterId iterator;
};
enum class IteratorMode { kSync, kAsync };
struct AcquireIterator {
  IteratorMode mode;
  std::uint16_t inline_cache;
};
struct ObjectCheck { abc::RegisterId value; };
struct CloseIterator {
  std::uint16_t inline_cache;
  abc::RegisterId iterator;
};
struct IteratorNext {
  abc::RegisterId iterator;
  abc::RegisterId method;
};
enum class IteratorError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using IteratorInstruction = std::variant<PropertyIterator, NextPropertyName,
                                         AcquireIterator, CloseIterator,
                                         IteratorNext, ObjectCheck, IteratorError>;

// Pinned v7 interpreter semantics: acquisition reads acc, property-name next
// reads its iterator register, close reads only its iterator register, and
// legacy next reads two registers; all replace acc on
// normal completion and may throw. ObjectCheck only reads its register, may
// throw TypeError and never defines acc. IC slots are not source operands. The ISA
// group's x_none exception annotation does not override interpreter handlers.
[[nodiscard]] IteratorInstruction LiftIteratorInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ITERATOR_H_
