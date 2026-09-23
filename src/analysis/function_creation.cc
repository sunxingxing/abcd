#include "abcd/analysis/function_creation.h"

#include <array>

namespace abcd::analysis {
std::variant<FunctionCreation, CreationError> LiftFunctionCreation(
    const abc::DecodedInstruction& instruction) {
  using abc::Opcode;
  CreationKind kind;
  std::uint64_t cache_max;
  constexpr std::uint64_t kNarrowCacheMax = UINT8_MAX;
  constexpr std::uint64_t kWideCacheMax = UINT16_MAX;
  switch (instruction.opcode) {
    case Opcode::kDEFINEFUNC_IMM8_ID16_IMM8:
      kind = CreationKind::kFunction;
      cache_max = kNarrowCacheMax;
      break;
    case Opcode::kDEFINEFUNC_IMM16_ID16_IMM8:
      kind = CreationKind::kFunction;
      cache_max = kWideCacheMax;
      break;
    case Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8:
      kind = CreationKind::kMethod;
      cache_max = kNarrowCacheMax;
      break;
    case Opcode::kDEFINEMETHOD_IMM16_ID16_IMM8:
      kind = CreationKind::kMethod;
      cache_max = kWideCacheMax;
      break;
    default:
      return CreationError::kUnsupportedOpcode;
  }
  // Pinned interpreter-inl.cpp handlers near 5080-5176: cache, method, length.
  // Cache slots are validated but not turned into language-level semantics.
  constexpr std::array kinds{abc::OperandKind::kImmediate,
                             abc::OperandKind::kMethod,
                             abc::OperandKind::kImmediate};
  constexpr std::uint64_t kMethodSlotMax = UINT16_MAX;
  constexpr std::uint64_t kFunctionLengthMax = UINT8_MAX;
  const std::array maxima{cache_max, kMethodSlotMax, kFunctionLengthMax};
  if (instruction.operand_count != kinds.size())
    return CreationError::kMalformedInstruction;
  for (std::size_t i = 0; i < kinds.size(); ++i)
    if (instruction.operands[i].kind != kinds[i] ||
        instruction.operands[i].raw_bits > maxima[i])
      return CreationError::kMalformedInstruction;
  constexpr std::size_t kMethodOperand = 1;
  constexpr std::size_t kLengthOperand = 2;
  return FunctionCreation{kind,
                          {static_cast<std::uint32_t>(
                              instruction.operands[kMethodOperand].raw_bits)},
                          {static_cast<std::uint8_t>(
                              instruction.operands[kLengthOperand].raw_bits)},
                          kind == CreationKind::kFunction
                              ? HomeObjectInput::kCurrentFunction
                              : HomeObjectInput::kAccumulator,
                          static_cast<std::uint16_t>(
                              instruction.operands[0].raw_bits)};
}
}  // namespace abcd::analysis
