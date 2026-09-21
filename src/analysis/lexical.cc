#include "abcd/analysis/lexical.h"

namespace abcd::analysis {
namespace {
enum class Action {
  kLoad,
  kStore,
  kRegisterStore,
  kPush,
  kNamedPush,
  kPop,
  kCurrent
};
struct Spec {
  Action action;
  std::uint64_t immediate_max;
};
std::optional<Spec> Describe(abc::Opcode opcode) {
  using abc::Opcode;
  constexpr std::uint64_t kNibbleMax = 0xf;
  constexpr std::uint64_t kByteMax = UINT8_MAX;
  constexpr std::uint64_t kWordMax = UINT16_MAX;
  switch (opcode) {
    case Opcode::kLDLEXVAR_IMM4_IMM4:
      return Spec{Action::kLoad, kNibbleMax};
    case Opcode::kLDLEXVAR_IMM8_IMM8:
      return Spec{Action::kLoad, kByteMax};
    case Opcode::kWIDE_LDLEXVAR_PREF_IMM16_IMM16:
      return Spec{Action::kLoad, kWordMax};
    case Opcode::kSTLEXVAR_IMM4_IMM4:
      return Spec{Action::kStore, kNibbleMax};
    case Opcode::kSTLEXVAR_IMM8_IMM8:
      return Spec{Action::kStore, kByteMax};
    case Opcode::kWIDE_STLEXVAR_PREF_IMM16_IMM16:
      return Spec{Action::kStore, kWordMax};
    case Opcode::kDEPRECATED_STLEXVAR_PREF_IMM4_IMM4_V8:
      return Spec{Action::kRegisterStore, kNibbleMax};
    case Opcode::kDEPRECATED_STLEXVAR_PREF_IMM8_IMM8_V8:
      return Spec{Action::kRegisterStore, kByteMax};
    case Opcode::kDEPRECATED_STLEXVAR_PREF_IMM16_IMM16_V8:
      return Spec{Action::kRegisterStore, kWordMax};
    case Opcode::kNEWLEXENV_IMM8:
      return Spec{Action::kPush, kByteMax};
    case Opcode::kWIDE_NEWLEXENV_PREF_IMM16:
      return Spec{Action::kPush, kWordMax};
    case Opcode::kNEWLEXENVWITHNAME_IMM8_ID16:
      return Spec{Action::kNamedPush, kByteMax};
    case Opcode::kWIDE_NEWLEXENVWITHNAME_PREF_IMM16_ID16:
      return Spec{Action::kNamedPush, kWordMax};
    case Opcode::kPOPLEXENV:
    case Opcode::kDEPRECATED_POPLEXENV_PREF_NONE:
      return Spec{Action::kPop, 0};
    case Opcode::kDEPRECATED_LDLEXENV_PREF_NONE:
      return Spec{Action::kCurrent, 0};
    default:
      return std::nullopt;
  }
}
}  // namespace

LexicalResult LiftLexicalInstruction(const abc::DecodedInstruction& instruction,
                                     std::uint64_t frame_size) {
  const auto spec = Describe(instruction.opcode);
  if (!spec) return LexicalError::kUnsupportedOpcode;
  const auto action = spec->action;
  constexpr std::size_t kAddressOperands = 2;
  constexpr std::size_t kStoreRegisterOperand = 2;
  std::size_t count = 0;
  switch (action) {
    case Action::kLoad:
    case Action::kStore:
    case Action::kNamedPush:
      count = kAddressOperands;
      break;
    case Action::kRegisterStore:
      count = kAddressOperands + 1;
      break;
    case Action::kPush:
      count = 1;
      break;
    case Action::kPop:
    case Action::kCurrent:
      break;
  }
  if (instruction.operand_count != count)
    return LexicalError::kMalformedInstruction;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& operand = instruction.operands[i];
    auto kind = abc::OperandKind::kImmediate;
    auto maximum = spec->immediate_max;
    if (action == Action::kRegisterStore && i == kStoreRegisterOperand) {
      kind = abc::OperandKind::kRegister;
      maximum = UINT8_MAX;
    } else if (action == Action::kNamedPush && i == 1) {
      kind = abc::OperandKind::kLiteralArray;
      maximum = UINT16_MAX;
    }
    if (operand.kind != kind || operand.raw_bits > maximum)
      return LexicalError::kMalformedInstruction;
    if (kind == abc::OperandKind::kRegister && operand.raw_bits >= frame_size)
      return LexicalError::kRegisterRange;
  }
  if (action == Action::kPop) return PopEnvironment{};
  if (action == Action::kCurrent) return CurrentEnvironment{};
  const auto first =
      static_cast<std::uint16_t>(instruction.operands[0].raw_bits);
  if (action == Action::kPush || action == Action::kNamedPush) {
    PushEnvironment push{first, {}};
    if (action == Action::kNamedPush)
      push.scope = abc::PoolIndex{
          static_cast<std::uint32_t>(instruction.operands[1].raw_bits)};
    return push;
  }
  const LexicalAddress address{
      {first}, {static_cast<std::uint16_t>(instruction.operands[1].raw_bits)}};
  if (action == Action::kLoad) return LexicalLoad{address};
  // Deprecated stores read the explicit register, not the accumulator. Keep
  // this choice typed even when a later optimization proves values identical.
  ScalarSlot value = Accumulator{};
  if (action == Action::kRegisterStore)
    value = abc::RegisterId{static_cast<std::uint32_t>(
        instruction.operands[kStoreRegisterOperand].raw_bits)};
  return LexicalStore{address, value};
}
}  // namespace abcd::analysis
