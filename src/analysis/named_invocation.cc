#include "abcd/analysis/named_invocation.h"

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
std::variant<ReceiverCall, CallError> LiftReceiverCall(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  std::uint8_t count = 0;
  switch (instruction.opcode) {
    case abc::Opcode::kCALLTHIS0_IMM8_V8:
      break;
    case abc::Opcode::kCALLTHIS1_IMM8_V8_V8:
      count = 1;
      break;
    case abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8:
      count = 2;
      break;
    case abc::Opcode::kCALLTHIS3_IMM8_V8_V8_V8_V8:
      count = 3;
      break;
    default:
      return CallError::kUnsupportedOpcode;
  }
  constexpr std::size_t kCache = 0;
  constexpr std::size_t kReceiver = 1;
  constexpr std::size_t kArguments = 2;
  if (instruction.operand_count != kArguments + count ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > UINT8_MAX)
    return CallError::kMalformedInstruction;
  ReceiverCall result{};
  result.arguments.count = count;
  for (std::size_t i = kReceiver; i < kArguments + count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister ||
        operand.raw_bits > UINT8_MAX)
      return CallError::kMalformedInstruction;
    if (operand.raw_bits >= frame_size) return CallError::kRegisterRange;
    const abc::RegisterId reg{static_cast<std::uint32_t>(operand.raw_bits)};
    if (i == kReceiver)
      result.receiver = reg;
    else
      result.arguments.values[i - kArguments] = reg;
  }
  return result;
}

std::optional<NamedInvocation> MatchNamedInvocation(
    std::span<const abc::BodyInstruction> block, std::size_t property,
    std::uint64_t frame_size) {
  if (property == 0 || property >= block.size() || property + 1 >= block.size())
    return {};
  const auto read = LiftNamedRead(block[property].decoded);
  const auto* named = std::get_if<NamedRead>(&read);
  if (!named) return {};
  const auto copied =
      LiftScalarInstruction(block[property - 1].decoded, frame_size);
  const auto* copy = std::get_if<ScalarAssignment>(&copied);
  if (!copy) return {};
  const auto* value = std::get_if<ScalarValue>(&copy->expression);
  if (!value) return {};
  std::optional<abc::RegisterId> receiver;
  if (std::holds_alternative<Accumulator>(copy->destination)) {
    const auto* reg = std::get_if<abc::RegisterId>(value);
    if (!reg) return {};
    receiver = *reg;
  } else {
    if (!std::holds_alternative<Accumulator>(*value)) return {};
    receiver = std::get<abc::RegisterId>(copy->destination);
  }
  const auto direct = LiftReceiverCall(block[property + 1].decoded, frame_size);
  if (const auto* call = std::get_if<ReceiverCall>(&direct)) {
    if (call->receiver != *receiver) return {};
    return NamedInvocation{*named, *call, 2};
  }

  // The v7.0 translator commonly spills a named callee through one register
  // before CALLTHIS. Require the exact typed store/load pair and no intervening
  // operation; this preserves one property read and the original receiver.
  if (property + 3 >= block.size()) return {};
  const auto stored =
      LiftScalarInstruction(block[property + 1].decoded, frame_size);
  const auto* store = std::get_if<ScalarAssignment>(&stored);
  const auto* store_value =
      store ? std::get_if<ScalarValue>(&store->expression) : nullptr;
  if (!store || !store_value ||
      !std::holds_alternative<abc::RegisterId>(store->destination) ||
      !std::holds_alternative<Accumulator>(*store_value))
    return {};
  const auto loaded =
      LiftScalarInstruction(block[property + 2].decoded, frame_size);
  const auto* load = std::get_if<ScalarAssignment>(&loaded);
  const auto* load_value =
      load ? std::get_if<ScalarValue>(&load->expression) : nullptr;
  if (!load || !load_value ||
      !std::holds_alternative<Accumulator>(load->destination) ||
      !std::holds_alternative<abc::RegisterId>(*load_value) ||
      std::get<abc::RegisterId>(*load_value) !=
          std::get<abc::RegisterId>(store->destination))
    return {};
  const auto spilled_call =
      LiftReceiverCall(block[property + 3].decoded, frame_size);
  const auto* call = std::get_if<ReceiverCall>(&spilled_call);
  if (!call || call->receiver != *receiver) return {};
  const auto temporary = std::get<abc::RegisterId>(store->destination);
  if (temporary == call->receiver) return {};
  for (std::size_t i = 0; i < call->arguments.count; ++i)
    if (temporary == call->arguments.values[i]) return {};
  // Eliminating the spill is valid only when its value cannot escape through
  // later register reads or a successor block. These bounded tails prove that
  // the temporary is dead without a per-call liveness scan.
  constexpr std::size_t kSpilledSequence = 4;
  constexpr std::size_t kDirectTerminal = 1;
  constexpr std::size_t kReloadedTerminal = 2;
  const auto tail = block.size() - property - kSpilledSequence;
  if (tail != kDirectTerminal && tail != kReloadedTerminal) return {};
  const auto& terminal = block.back().decoded;
  if (terminal.operand_count != 0 ||
      (terminal.opcode != abc::Opcode::kRETURN &&
       terminal.opcode != abc::Opcode::kRETURNUNDEFINED &&
       terminal.opcode != abc::Opcode::kTHROW_PREF_NONE))
    return {};
  if (tail == kReloadedTerminal) {
    const auto reloaded =
        LiftScalarInstruction(block[block.size() - kReloadedTerminal].decoded,
                              frame_size);
    const auto* assignment = std::get_if<ScalarAssignment>(&reloaded);
    const auto* value = assignment
                            ? std::get_if<ScalarValue>(&assignment->expression)
                            : nullptr;
    const auto* reg = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    if (!assignment ||
        !std::holds_alternative<Accumulator>(assignment->destination) ||
        !reg || *reg == temporary)
      return {};
  }
  return NamedInvocation{*named, *call, 4};
}
}  // namespace abcd::analysis
