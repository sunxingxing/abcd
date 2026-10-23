#include "instance_initialization.h"

#include <vector>

namespace abcd::source {
namespace {
using Error = InstanceInitializationError;
bool LexicalLoad(const abc::DecodedInstruction& instruction, std::uint64_t& depth, std::uint64_t& slot) {
  switch (instruction.opcode) {
    case abc::Opcode::kLDLEXVAR_IMM4_IMM4:
    case abc::Opcode::kLDLEXVAR_IMM8_IMM8:
    case abc::Opcode::kWIDE_LDLEXVAR_PREF_IMM16_IMM16:
      if (instruction.operand_count != 2) return false;
      depth = instruction.operands[0].raw_bits; slot = instruction.operands[1].raw_bits;
      return true;
    default: return false;
  }
}
}  // namespace
std::variant<std::size_t, InstanceInitializationError> ProjectInstanceInitialization(
    abc::MethodBody& body, const abc::FrameLayout& frame, analysis::LexicalSlot slot) {
  if (!frame.this_value) return Error::kReceiver;
  // Registers known to hold the entry this; the accumulator separately.
  std::vector<std::uint8_t> holds(frame.registers, 0);
  if (frame.this_value->value >= holds.size()) return Error::kReceiver;
  holds[frame.this_value->value] = 1;
  bool acc_this = false;
  std::size_t at = 0;
  const auto reg = [&](const abc::InstructionOperand& operand) -> std::optional<std::size_t> {
    if (operand.kind != abc::OperandKind::kRegister || operand.raw_bits >= holds.size()) return {};
    return static_cast<std::size_t>(operand.raw_bits);
  };
  for (; at < body.instructions.size(); ++at) {
    const auto& decoded = body.instructions[at].decoded;
    std::uint64_t depth = 0, index = 0;
    if (LexicalLoad(decoded, depth, index)) break;
    switch (decoded.opcode) {
      case abc::Opcode::kMOV_V4_V4: case abc::Opcode::kMOV_V8_V8: case abc::Opcode::kMOV_V16_V16: {
        const auto to = reg(decoded.operands[0]), from = reg(decoded.operands[1]);
        if (decoded.operand_count != 2 || !to || !from) return Error::kPrefix;
        holds[*to] = holds[*from]; break;
      }
      case abc::Opcode::kLDA_V8: {
        const auto from = reg(decoded.operands[0]);
        if (decoded.operand_count != 1 || !from) return Error::kPrefix;
        acc_this = holds[*from]; break;
      }
      case abc::Opcode::kSTA_V8: {
        const auto to = reg(decoded.operands[0]);
        if (decoded.operand_count != 1 || !to) return Error::kPrefix;
        holds[*to] = acc_this; break;
      }
      default: return Error::kPrefix;
    }
  }
  std::uint64_t depth = 0, index = 0;
  if (at + 2 >= body.instructions.size() || !LexicalLoad(body.instructions[at].decoded, depth, index) ||
      depth != 0 || index != slot.value) return Error::kLoad;
  const auto& call = body.instructions[at + 1].decoded;
  if (call.opcode != abc::Opcode::kCALLRUNTIME_CALLINIT_PREF_IMM8_V8 || call.operand_count != 2) return Error::kCall;
  const auto receiver = reg(call.operands[1]);
  if (!receiver || !holds[*receiver]) return Error::kReceiver;
  if (body.instructions[at + 2].decoded.accumulator_read) return Error::kAccumulator;
  const auto call_pc = body.instructions[at + 1].offset.value;
  for (const auto& instruction : body.instructions) {
    if (instruction.branch_target && instruction.branch_target->value <= call_pc) return Error::kReentry;
    std::uint64_t d = 0, s = 0;
    if (&instruction != &body.instructions[at] && LexicalLoad(instruction.decoded, d, s) && s == slot.value)
      return Error::kDuplicate;
  }
  for (const auto& region : body.tries)
    if (region.offset.value <= call_pc) return Error::kReentry;
  for (const auto& handler : body.handlers)
    if (handler.offset.value <= call_pc) return Error::kReentry;
  for (const auto i : {at, at + 1}) {
    auto& decoded = body.instructions[i].decoded;
    const auto size = decoded.size;
    decoded = abc::DecodedInstruction{};
    decoded.opcode = abc::Opcode::kNOP; decoded.size = size;
  }
  return at + 1;
}
}  // namespace abcd::source
