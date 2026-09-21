#include "abcd/analysis/hole_guard.h"

namespace abcd::analysis {
HoleResult LiftHoleInstruction(const abc::DecodedInstruction& instruction,
                               std::uint64_t frame_size) {
  using abc::Opcode;
  switch (instruction.opcode) {
    case Opcode::kLDHOLE:
      if (instruction.operand_count != 0)
        return HoleError::kMalformedInstruction;
      return LoadHole{};
    case Opcode::kTHROW_UNDEFINEDIFHOLE_PREF_V8_V8: {
      constexpr std::size_t kRegisterOperands = 2;
      constexpr std::uint64_t kRegisterMax = UINT8_MAX;
      if (instruction.operand_count != kRegisterOperands)
        return HoleError::kMalformedInstruction;
      for (std::size_t i = 0; i < kRegisterOperands; ++i) {
        const auto& operand = instruction.operands[i];
        if (operand.kind != abc::OperandKind::kRegister ||
            operand.raw_bits > kRegisterMax)
          return HoleError::kMalformedInstruction;
        if (operand.raw_bits >= frame_size) return HoleError::kRegisterRange;
      }
      constexpr std::size_t kValueOperand = 0;
      constexpr std::size_t kNameOperand = 1;
      return HoleGuard{abc::RegisterId{static_cast<std::uint32_t>(
                           instruction.operands[kValueOperand].raw_bits)},
                       abc::RegisterId{static_cast<std::uint32_t>(
                           instruction.operands[kNameOperand].raw_bits)}};
    }
    case Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16: {
      constexpr std::uint64_t kNameSlotMax = UINT16_MAX;
      if (instruction.operand_count != 1 ||
          instruction.operands[0].kind != abc::OperandKind::kString ||
          instruction.operands[0].raw_bits > kNameSlotMax)
        return HoleError::kMalformedInstruction;
      return HoleGuard{Accumulator{}, HoleNameSlot{{static_cast<std::uint32_t>(
                                          instruction.operands[0].raw_bits)}}};
    }
    default:
      return HoleError::kUnsupportedOpcode;
  }
}
}  // namespace abcd::analysis
