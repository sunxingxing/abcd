#include "abcd/analysis/iter_result.h"

#include <climits>

namespace abcd::analysis {
std::variant<IterResult, IterResultError> LiftIterResult(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  if (instruction.opcode != abc::Opcode::kCREATEITERRESULTOBJ_V8_V8)
    return IterResultError::kUnsupportedOpcode;
  constexpr std::size_t kValue = 0;
  constexpr std::size_t kDone = 1;
  if (instruction.operand_count != 2 ||
      instruction.operands[kValue].kind != abc::OperandKind::kRegister ||
      instruction.operands[kDone].kind != abc::OperandKind::kRegister ||
      instruction.operands[kValue].raw_bits > UINT8_MAX ||
      instruction.operands[kDone].raw_bits > UINT8_MAX)
    return IterResultError::kMalformedInstruction;
  if (instruction.operands[kValue].raw_bits >= frame_size ||
      instruction.operands[kDone].raw_bits >= frame_size)
    return IterResultError::kRegisterRange;
  return IterResult{
      abc::RegisterId{static_cast<std::uint32_t>(
          instruction.operands[kValue].raw_bits)},
      abc::RegisterId{static_cast<std::uint32_t>(
          instruction.operands[kDone].raw_bits)}};
}
}  // namespace abcd::analysis
