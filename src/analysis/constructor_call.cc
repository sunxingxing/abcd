#include "abcd/analysis/constructor_call.h"

namespace abcd::analysis {
std::uint32_t ConstructorCall::ArgumentCount() const {
  return inputs.count == 0 ? 0 : inputs.count - 1;
}

std::optional<abc::RegisterId> ConstructorCall::ArgumentAt(
    std::uint32_t position) const {
  if (position >= ArgumentCount()) return {};
  return abc::RegisterId{inputs.first.value + position + 1};
}

std::variant<ConstructorCall, ConstructorCallError> LiftConstructorCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  constexpr std::size_t kNarrowCache = 0;
  constexpr std::size_t kNarrowCount = 1;
  constexpr std::size_t kNarrowFirst = 2;
  constexpr std::size_t kWideCount = 0;
  constexpr std::size_t kWideFirst = 1;
  constexpr std::size_t kNarrowOperands = 3;
  constexpr std::size_t kWideOperands = 2;
  enum class Form { kByteCache, kWordCache, kNoCache };
  Form form;
  switch (instruction.opcode) {
    case Opcode::kNEWOBJRANGE_IMM8_IMM8_V8:
      form = Form::kByteCache;
      break;
    case Opcode::kNEWOBJRANGE_IMM16_IMM8_V8:
      form = Form::kWordCache;
      break;
    case Opcode::kWIDE_NEWOBJRANGE_PREF_IMM16_V8:
      form = Form::kNoCache;
      break;
    default:
      return ConstructorCallError::kUnsupportedOpcode;
  }
  const bool wide = form == Form::kNoCache;
  if (instruction.operand_count !=
      (wide ? kWideOperands : kNarrowOperands))
    return ConstructorCallError::kMalformedInstruction;
  const auto& count = instruction.operands[wide ? kWideCount : kNarrowCount];
  const auto& first = instruction.operands[wide ? kWideFirst : kNarrowFirst];
  if (count.kind != abc::OperandKind::kImmediate ||
      count.raw_bits > (wide ? UINT16_MAX : UINT8_MAX) ||
      first.kind != abc::OperandKind::kRegister ||
      first.raw_bits > UINT8_MAX)
    return ConstructorCallError::kMalformedInstruction;
  std::optional<std::uint16_t> profile_slot;
  if (!wide) {
    const auto& cache = instruction.operands[kNarrowCache];
    if (cache.kind != abc::OperandKind::kImmediate ||
        cache.raw_bits >
            (form == Form::kByteCache ? UINT8_MAX : UINT16_MAX))
      return ConstructorCallError::kMalformedInstruction;
    profile_slot = static_cast<std::uint16_t>(cache.raw_bits);
  }
  if (count.raw_bits == 0) return ConstructorCallError::kEmptyRange;
  if (first.raw_bits >= frame_size ||
      count.raw_bits > frame_size - first.raw_bits)
    return ConstructorCallError::kRegisterRange;
  return ConstructorCall{
      CallRange{{static_cast<std::uint32_t>(first.raw_bits)},
                static_cast<std::uint16_t>(count.raw_bits)},
      profile_slot};
}
}  // namespace abcd::analysis
