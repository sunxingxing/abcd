#include "abcd/abc/register_access.h"

namespace abcd::abc {
namespace {
enum class WindowRule {
  kNone,
  kCount,
  kCountPlusOne,
  kNonemptyCount,
  kNonemptyCountPlusOne
};

WindowRule RangeRule(Opcode opcode) {
  // Interpreter CALL_* macros, NEWOBJRANGE and
  // RuntimeCreateObjectWithExcludedKeys are authoritative. ISA range_0 alone
  // omits receiver/callee reads in several encodings. Keep the regression table
  // in register_access_test in sync.
  switch (opcode) {
    case Opcode::kCALLRANGE_IMM8_IMM8_V8:
    case Opcode::kWIDE_CALLRANGE_PREF_IMM16_V8:
    case Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8:
    case Opcode::kWIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8:
    case Opcode::kSUPERCALLARROWRANGE_IMM8_IMM8_V8:
    case Opcode::kWIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8:
      return WindowRule::kCount;
    case Opcode::kCALLTHISRANGE_IMM8_IMM8_V8:
    case Opcode::kWIDE_CALLTHISRANGE_PREF_IMM16_V8:
    case Opcode::kCALLTHISRANGEWITHNAME_IMM8_IMM8_ID16_V8:
    case Opcode::kWIDE_CALLTHISRANGEWITHNAME_PREF_IMM16_ID16_V8:
    case Opcode::kDEPRECATED_CALLRANGE_PREF_IMM16_V8:
    case Opcode::kCREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8:
    case Opcode::kWIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8:
      return WindowRule::kCountPlusOne;
    case Opcode::kNEWOBJRANGE_IMM8_IMM8_V8:
    case Opcode::kNEWOBJRANGE_IMM16_IMM8_V8:
    case Opcode::kWIDE_NEWOBJRANGE_PREF_IMM16_V8:
      return WindowRule::kNonemptyCount;
    case Opcode::kDEPRECATED_CALLTHISRANGE_PREF_IMM16_V8:
      return WindowRule::kNonemptyCountPlusOne;
    default:
      return WindowRule::kNone;
  }
}
}  // namespace

RegisterAccessResult GetRegisterAccesses(const DecodedInstruction& instruction,
                                         std::uint64_t frame_size) {
  if (instruction.operand_count > instruction.operands.size())
    return RegisterAccessError::kMalformedInstruction;
  const auto rule = RangeRule(instruction.opcode);
  std::optional<std::size_t> range_base;
  std::optional<std::size_t> range_count;
  for (std::size_t i = 0; i < instruction.operand_count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind == OperandKind::kRegister) range_base = i;
    if (operand.kind == OperandKind::kImmediate) range_count = i;
  }
  std::uint64_t window_count = 0;
  if (rule != WindowRule::kNone) {
    if (!range_base || !range_count)
      return RegisterAccessError::kMalformedInstruction;
    window_count = instruction.operands[*range_count].raw_bits;
    if (window_count > UINT16_MAX) return RegisterAccessError::kInvalidCount;
    if ((rule == WindowRule::kNonemptyCount ||
         rule == WindowRule::kNonemptyCountPlusOne) &&
        window_count == 0)
      return RegisterAccessError::kInvalidCount;
    if (rule == WindowRule::kCountPlusOne ||
        rule == WindowRule::kNonemptyCountPlusOne)
      ++window_count;
  }
  RegisterAccesses result;
  result.accumulator_read = instruction.accumulator_read;
  result.accumulator_write = instruction.accumulator_write;
  for (std::size_t i = 0; i < instruction.operand_count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != OperandKind::kRegister) continue;
    const auto count =
        rule != WindowRule::kNone && range_base == i ? window_count : 1;
    if (count == 0) continue;
    if (operand.raw_bits > UINT32_MAX || operand.raw_bits >= frame_size ||
        count > frame_size - operand.raw_bits)
      return RegisterAccessError::kFrameRange;
    result.windows[result.count++] = {
        RegisterId{static_cast<std::uint32_t>(operand.raw_bits)},
        static_cast<std::uint32_t>(count), operand.access};
  }
  return result;
}
}  // namespace abcd::abc
