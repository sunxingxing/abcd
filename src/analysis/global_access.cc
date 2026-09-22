#include "abcd/analysis/global_access.h"

namespace abcd::analysis {
std::variant<GlobalAccess, GlobalAccessError> LiftGlobalAccess(
    const abc::DecodedInstruction& instruction) {
  GlobalOp operation;
  std::uint64_t cache_max = UINT16_MAX;
  using abc::Opcode;
  switch (instruction.opcode) {
    case Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16:
      cache_max = UINT8_MAX;
      [[fallthrough]];
    case Opcode::kTRYLDGLOBALBYNAME_IMM16_ID16:
      operation = GlobalOp::kReadName;
      break;
    case Opcode::kTRYSTGLOBALBYNAME_IMM8_ID16:
      cache_max = UINT8_MAX;
      [[fallthrough]];
    case Opcode::kTRYSTGLOBALBYNAME_IMM16_ID16:
      operation = GlobalOp::kWriteName;
      break;
    case Opcode::kLDGLOBALVAR_IMM16_ID16:
      operation = GlobalOp::kReadVar;
      break;
    case Opcode::kSTGLOBALVAR_IMM16_ID16:
      operation = GlobalOp::kWriteVar;
      break;
    case Opcode::kSTCONSTTOGLOBALRECORD_IMM16_ID16:
      operation = GlobalOp::kInitConst;
      break;
    case Opcode::kSTTOGLOBALRECORD_IMM16_ID16:
      operation = GlobalOp::kInitMutable;
      break;
    default:
      return GlobalAccessError::kUnsupportedOpcode;
  }
  constexpr std::size_t kCache = 0, kName = 1, kOperands = 2;
  if (instruction.operand_count != kOperands ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > cache_max ||
      instruction.operands[kName].kind != abc::OperandKind::kString ||
      instruction.operands[kName].raw_bits > UINT16_MAX)
    return GlobalAccessError::kMalformedInstruction;
  return GlobalAccess{
      operation,
      {static_cast<std::uint32_t>(instruction.operands[kName].raw_bits)}};
}
bool ReadsGlobal(GlobalOp operation) {
  switch (operation) {
    case GlobalOp::kReadName:
    case GlobalOp::kReadVar:
      return true;
    case GlobalOp::kWriteName:
    case GlobalOp::kWriteVar:
    case GlobalOp::kInitConst:
    case GlobalOp::kInitMutable:
      return false;
  }
  std::terminate();
}
}  // namespace abcd::analysis
