#include "abcd/analysis/coroutine.h"

#include <cstddef>
#include <optional>

namespace abcd::analysis {
namespace {
// Every admitted explicit operand is v8 in the pinned ISA. Validate encoding
// before frame membership, including deprecated prefixed layouts.
std::optional<CoroutineError> CheckRegisters(
    const abc::DecodedInstruction& instruction, std::size_t count,
    std::uint64_t frame_size) {
  if (instruction.operand_count != count)
    return CoroutineError::kMalformedInstruction;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister ||
        operand.raw_bits > UINT8_MAX)
      return CoroutineError::kMalformedInstruction;
  }
  for (std::size_t i = 0; i < count; ++i)
    if (instruction.operands[i].raw_bits >= frame_size)
      return CoroutineError::kRegisterRange;
  return std::nullopt;
}
abc::RegisterId Register(const abc::DecodedInstruction& instruction,
                         std::size_t index) {
  return {static_cast<std::uint32_t>(instruction.operands[index].raw_bits)};
}
CoroutineResult Transfer(const abc::DecodedInstruction& instruction,
                         std::uint64_t frame_size,
                         CoroutineTransferKind kind, bool explicit_value) {
  constexpr std::size_t kState = 0, kValue = 1;
  constexpr std::size_t kStateOnly = 1, kStateAndValue = 2;
  if (const auto error = CheckRegisters(
          instruction, explicit_value ? kStateAndValue : kStateOnly,
          frame_size))
    return *error;
  return CoroutineTransfer{
      kind, Register(instruction, kState),
      explicit_value ? ScalarSlot{Register(instruction, kValue)}
                     : ScalarSlot{Accumulator{}}};
}
CoroutineResult Read(const abc::DecodedInstruction& instruction,
                     std::uint64_t frame_size, CoroutineReadKind kind,
                     bool explicit_state) {
  constexpr std::size_t kState = 0;
  constexpr std::size_t kNoOperands = 0, kStateOnly = 1;
  if (const auto error = CheckRegisters(
          instruction, explicit_state ? kStateOnly : kNoOperands, frame_size))
    return *error;
  return CoroutineRead{
      kind, explicit_state ? ScalarSlot{Register(instruction, kState)}
                           : ScalarSlot{Accumulator{}}};
}
}  // namespace

CoroutineResult LiftCoroutineInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  using abc::Opcode;
  using enum CoroutineTransferKind;
  using enum CoroutineReadKind;
  switch (instruction.opcode) {
    case Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8:
      return Transfer(instruction, frame_size, kAwait, false);
    case Opcode::kDEPRECATED_ASYNCFUNCTIONAWAITUNCAUGHT_PREF_V8_V8:
      return Transfer(instruction, frame_size, kAwait, true);
    case Opcode::kSUSPENDGENERATOR_V8:
      return Transfer(instruction, frame_size, kSuspend, false);
    case Opcode::kDEPRECATED_SUSPENDGENERATOR_PREF_V8_V8:
      return Transfer(instruction, frame_size, kSuspend, true);
    case Opcode::kASYNCFUNCTIONRESOLVE_V8:
      return Transfer(instruction, frame_size, kResolve, false);
    case Opcode::kASYNCFUNCTIONREJECT_V8:
      return Transfer(instruction, frame_size, kReject, false);
    case Opcode::kRESUMEGENERATOR:
      return Read(instruction, frame_size, kResumeValue, false);
    case Opcode::kDEPRECATED_RESUMEGENERATOR_PREF_V8:
      return Read(instruction, frame_size, kResumeValue, true);
    case Opcode::kGETRESUMEMODE:
      return Read(instruction, frame_size, kResumeMode, false);
    case Opcode::kDEPRECATED_GETRESUMEMODE_PREF_V8:
      return Read(instruction, frame_size, kResumeMode, true);
    default:
      return CoroutineError::kUnsupportedOpcode;
  }
}
}  // namespace abcd::analysis
