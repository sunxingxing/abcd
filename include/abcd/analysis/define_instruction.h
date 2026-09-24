#ifndef ABCD_ANALYSIS_DEFINE_INSTRUCTION_H_
#define ABCD_ANALYSIS_DEFINE_INSTRUCTION_H_

#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct DefineFieldByValue {
  std::uint8_t profile_slot = 0;
  abc::RegisterId key;
  abc::RegisterId object;
};
struct DefineFieldByIndex {
  std::uint8_t profile_slot = 0;
  std::uint32_t index = 0;
  abc::RegisterId object;
};
struct DefineAccessor {
  abc::RegisterId object;
  abc::RegisterId key;
  abc::RegisterId getter;
  abc::RegisterId setter;
  // Accumulator ToBoolean controls accessor function-name assignment, not
  // enumerability (which depends on class ownership). Result replaces acc.
};
struct DefinePrivateProperty {
  std::uint8_t reserved_operand = 0;
  std::uint16_t lexical_level = 0;
  std::uint16_t lexical_slot = 0;
  abc::RegisterId object;
};
struct DefineSendableClass {
  std::uint16_t reserved_operand = 0;
  abc::PoolIndex constructor;
  abc::PoolIndex literal;
  std::uint16_t length = 0;
  abc::RegisterId parent;
};
struct DefineLegacyClass {
  abc::PoolIndex constructor;
  std::uint16_t encoded_literal = 0;
  std::uint16_t length = 0;
  abc::RegisterId lexical_environment;
  abc::RegisterId parent;
  // The literal pool slot is constructor + 1 in the pinned legacy handler.
};
enum class DefineError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using DefineInstruction = std::variant<
    DefineFieldByValue, DefineFieldByIndex, DefineAccessor,
    DefinePrivateProperty, DefineSendableClass, DefineLegacyClass, DefineError>;

// Pinned v7 interpreter operands, after DecodeInstruction. This parses only
// the DEFINE* forms not covered by LiftFunctionCreation, LiftClassCreation or
// LiftOwnWrite. It does not assert JS source equivalence: private lexical slots,
// accessor flag/result, shared classes and legacy lexical environment require
// separate provenance before emission. No allocation, O(operands).
[[nodiscard]] DefineInstruction LiftDefineInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_DEFINE_INSTRUCTION_H_
