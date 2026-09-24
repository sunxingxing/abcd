#include "abcd/analysis/named_invocation.h"

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/register_access.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
std::uint32_t ReceiverCall::Count() const {
  return std::visit([](const auto& args) -> std::uint32_t {
    return args.count;
  }, arguments);
}

std::optional<abc::RegisterId> ReceiverCall::At(
    std::uint32_t position) const {
  if (position >= Count()) return {};
  if (const auto* fixed = std::get_if<CallRegisters>(&arguments))
    return fixed->values[position];
  const auto range = std::get<CallRange>(arguments);
  return abc::RegisterId{range.first.value + position};
}

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
    case abc::Opcode::kCALLTHISRANGE_IMM8_IMM8_V8: {
      constexpr std::size_t kRangeCount = 1;
      constexpr std::size_t kRangeReceiver = 2;
      if (instruction.operand_count != 3 ||
          instruction.operands[0].kind != abc::OperandKind::kImmediate ||
          instruction.operands[0].raw_bits > UINT8_MAX ||
          instruction.operands[kRangeCount].kind !=
              abc::OperandKind::kImmediate ||
          instruction.operands[kRangeCount].raw_bits > UINT8_MAX ||
          instruction.operands[kRangeReceiver].kind !=
              abc::OperandKind::kRegister ||
          instruction.operands[kRangeReceiver].raw_bits > UINT8_MAX)
        return CallError::kMalformedInstruction;
      const auto base = instruction.operands[kRangeReceiver].raw_bits;
      const auto range_count =
          instruction.operands[kRangeCount].raw_bits;
      if (base >= frame_size || range_count > frame_size - base - 1)
        return CallError::kRegisterRange;
      return ReceiverCall{
          abc::RegisterId{static_cast<std::uint32_t>(base)},
          CallRange{{static_cast<std::uint32_t>(base + 1)},
                    static_cast<std::uint8_t>(range_count)}};
    }
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
  CallRegisters arguments;
  arguments.count = count;
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
      arguments.values[i - kArguments] = reg;
  }
  result.arguments = arguments;
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
  for (std::uint32_t i = 0; i < call->Count(); ++i)
    if (temporary == *call->At(i)) return {};
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

std::optional<CopiedArgumentInvocation> MatchCopiedArgumentInvocation(
    std::span<const abc::BodyInstruction> block, std::size_t call,
    std::uint64_t frame_size) {
  constexpr std::size_t kPropertyToCall = 5;
  if (call <= kPropertyToCall || call + 1 >= block.size()) return {};
  const auto property = call - kPropertyToCall;
  const auto named = LiftNamedRead(block[property].decoded);
  const auto* read = std::get_if<NamedRead>(&named);
  if (!read) return {};
  const auto receiver_copy =
      LiftScalarInstruction(block[property - 1].decoded, frame_size);
  const auto* copied = std::get_if<ScalarAssignment>(&receiver_copy);
  const auto* copied_value =
      copied ? std::get_if<ScalarValue>(&copied->expression) : nullptr;
  const auto* receiver = copied_value
                             ? std::get_if<abc::RegisterId>(copied_value)
                             : nullptr;
  if (!copied || !receiver ||
      !std::holds_alternative<Accumulator>(copied->destination))
    return {};
  const auto stored =
      LiftScalarInstruction(block[property + 1].decoded, frame_size);
  const auto* spill = std::get_if<ScalarAssignment>(&stored);
  const auto* spill_value =
      spill ? std::get_if<ScalarValue>(&spill->expression) : nullptr;
  const auto* callee =
      spill ? std::get_if<abc::RegisterId>(&spill->destination) : nullptr;
  if (!spill || !spill_value || !callee ||
      !std::holds_alternative<Accumulator>(*spill_value) ||
      *callee == *receiver)
    return {};
  const auto loaded_argument =
      LiftScalarInstruction(block[property + 2].decoded, frame_size);
  const auto* load = std::get_if<ScalarAssignment>(&loaded_argument);
  const auto* load_value =
      load ? std::get_if<ScalarValue>(&load->expression) : nullptr;
  const auto* source =
      load_value ? std::get_if<abc::RegisterId>(load_value) : nullptr;
  if (!load || !source || *source == *callee ||
      !std::holds_alternative<Accumulator>(load->destination))
    return {};
  const auto stored_argument =
      LiftScalarInstruction(block[property + 3].decoded, frame_size);
  const auto* store = std::get_if<ScalarAssignment>(&stored_argument);
  const auto* store_value =
      store ? std::get_if<ScalarValue>(&store->expression) : nullptr;
  const auto* argument =
      store ? std::get_if<abc::RegisterId>(&store->destination) : nullptr;
  if (!store || !store_value || !argument ||
      !std::holds_alternative<Accumulator>(*store_value) ||
      *argument == *receiver || *argument == *callee)
    return {};
  const auto reloaded =
      LiftScalarInstruction(block[property + 4].decoded, frame_size);
  const auto* reload = std::get_if<ScalarAssignment>(&reloaded);
  const auto* reload_value =
      reload ? std::get_if<ScalarValue>(&reload->expression) : nullptr;
  const auto* reloaded_callee =
      reload_value ? std::get_if<abc::RegisterId>(reload_value) : nullptr;
  if (!reload || !reloaded_callee || *reloaded_callee != *callee ||
      !std::holds_alternative<Accumulator>(reload->destination))
    return {};
  const auto invoked = LiftReceiverCall(block[call].decoded, frame_size);
  const auto* call_value = std::get_if<ReceiverCall>(&invoked);
  if (!call_value || call_value->Count() != 1 ||
      call_value->receiver != *receiver || *call_value->At(0) != *argument)
    return {};
  const auto captured =
      LiftScalarInstruction(block[call + 1].decoded, frame_size);
  const auto* capture = std::get_if<ScalarAssignment>(&captured);
  const auto* capture_value =
      capture ? std::get_if<ScalarValue>(&capture->expression) : nullptr;
  const auto* destination =
      capture ? std::get_if<abc::RegisterId>(&capture->destination) : nullptr;
  if (!capture || !capture_value || !destination ||
      *destination != *callee ||
      !std::holds_alternative<Accumulator>(*capture_value))
    return {};
  return CopiedArgumentInvocation{
      property, property + 1, property + 4,
      NamedInvocation{*read, *call_value, 1}};
}

namespace {
std::optional<StagedNamedInvocation> MatchStagedAt(
    const abc::MethodBody& body, std::uint64_t frame_size,
    std::size_t call_index) {
  const auto& instructions = body.instructions;
  using abc::Opcode;
  if (call_index < 5 || call_index >= instructions.size()) return {};
  const auto lifted =
      LiftReceiverCall(instructions[call_index].decoded, frame_size);
  const auto* invoke = std::get_if<ReceiverCall>(&lifted);
  if (!invoke || invoke->Count() == 0) return {};
  const auto reload = call_index - 1;
  const auto loaded =
      LiftScalarInstruction(instructions[reload].decoded, frame_size);
  const auto* load = std::get_if<ScalarAssignment>(&loaded);
  const auto* load_value =
      load ? std::get_if<ScalarValue>(&load->expression) : nullptr;
  const auto* callee =
      load_value ? std::get_if<abc::RegisterId>(load_value) : nullptr;
  if (!load || !callee ||
      !std::holds_alternative<Accumulator>(load->destination) ||
      *callee == invoke->receiver)
    return {};
  for (std::uint32_t i = 0; i < invoke->Count(); ++i)
    if (*callee == *invoke->At(i)) return {};
  std::optional<std::size_t> spill;
  for (std::size_t i = reload; i-- > 0;) {
    const auto accesses =
        abc::GetRegisterAccesses(instructions[i].decoded, frame_size);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
    if (!known) return {};
    bool writes = false;
    for (std::size_t window = 0; window < known->count; ++window) {
      const auto& access = known->windows[window];
      if (callee->value < access.first.value ||
          callee->value - access.first.value >= access.count)
        continue;
      if (access.access == abc::OperandAccess::kRead)
        return {};
      writes = true;
    }
    if (writes) {
      spill = i;
      break;
    }
  }
  if (!spill || *spill < 2 || *spill + 2 >= reload) return {};
  const auto stored =
      LiftScalarInstruction(instructions[*spill].decoded, frame_size);
  const auto* store = std::get_if<ScalarAssignment>(&stored);
  const auto* store_value =
      store ? std::get_if<ScalarValue>(&store->expression) : nullptr;
  const auto* destination =
      store ? std::get_if<abc::RegisterId>(&store->destination) : nullptr;
  if (!store || !store_value || !destination || *destination != *callee ||
      !std::holds_alternative<Accumulator>(*store_value) ||
      instructions[*spill].decoded.opcode != Opcode::kSTA_V8)
    return {};
  const auto property = *spill - 1;
  const auto named = LiftNamedRead(instructions[property].decoded);
  const auto* read = std::get_if<NamedRead>(&named);
  if (!read) return {};
  const auto copied =
      LiftScalarInstruction(instructions[property - 1].decoded, frame_size);
  const auto* copy = std::get_if<ScalarAssignment>(&copied);
  const auto* copy_value =
      copy ? std::get_if<ScalarValue>(&copy->expression) : nullptr;
  const auto* receiver =
      copy_value ? std::get_if<abc::RegisterId>(copy_value) : nullptr;
  if (!copy || !receiver || *receiver != invoke->receiver ||
      !std::holds_alternative<Accumulator>(copy->destination))
    return {};
  const auto overwrite = LiftRegisterEffect(
      instructions[*spill + 1].decoded, frame_size);
  const auto* effect = std::get_if<RegisterEffect>(&overwrite);
  if (!effect || !effect->write ||
      !std::holds_alternative<Accumulator>(*effect->write))
    return {};
  for (std::size_t read_index = 0; read_index < effect->read_count;
       ++read_index)
    if (std::holds_alternative<Accumulator>(effect->reads[read_index]))
      return {};
  for (std::size_t i = *spill + 1; i < call_index; ++i) {
    const auto accesses =
        abc::GetRegisterAccesses(instructions[i].decoded, frame_size);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
    if (!known) return {};
    for (std::size_t window = 0; window < known->count; ++window) {
      const auto& access = known->windows[window];
      if (invoke->receiver.value >= access.first.value &&
          invoke->receiver.value - access.first.value < access.count &&
          access.access != abc::OperandAccess::kRead)
        return {};
    }
  }
  for (std::size_t i = call_index + 1; i < instructions.size(); ++i) {
    const auto accesses =
        abc::GetRegisterAccesses(instructions[i].decoded, frame_size);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
    if (!known) return {};
    bool killed = false;
    for (std::size_t window = 0; window < known->count; ++window) {
      const auto& access = known->windows[window];
      if (callee->value < access.first.value ||
          callee->value - access.first.value >= access.count)
        continue;
      if (access.access != abc::OperandAccess::kWrite) return {};
      killed = true;
    }
    if (killed)
      return StagedNamedInvocation{property, *spill, reload, call_index,
                                   *read, *invoke, *callee};
  }
  return StagedNamedInvocation{property, *spill, reload, call_index, *read,
                               *invoke, *callee};
}
}  // namespace

std::optional<StagedNamedInvocation> MatchTailNamedInvocation(
    const abc::MethodBody& body, std::uint64_t frame_size) {
  const auto& instructions = body.instructions;
  if (!body.tries.empty() || !body.handlers.empty() || instructions.size() < 7)
    return {};
  using abc::Opcode;
  const auto terminal = instructions.back().decoded.opcode;
  if (terminal != Opcode::kRETURN && terminal != Opcode::kRETURNUNDEFINED &&
      terminal != Opcode::kTHROW_PREF_NONE)
    return {};
  std::size_t call_index = instructions.size() - 1;
  while (call_index != 0) {
    const auto& suffix = instructions[call_index - 1].decoded;
    if (suffix.operand_count != 0 ||
        (suffix.opcode != Opcode::kNOP &&
         suffix.opcode != Opcode::kLDUNDEFINED))
      break;
    --call_index;
  }
  if (call_index == 0) return {};
  return MatchStagedAt(body, frame_size, call_index - 1);
}

std::optional<StagedNamedInvocation> MatchStagedNamedInvocation(
    const abc::MethodBody& body, std::uint64_t frame_size) {
  const auto& instructions = body.instructions;
  if (!body.tries.empty() || !body.handlers.empty() || instructions.empty())
    return {};
  std::optional<std::size_t> call;
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto transfer =
        abc::GetControlTransfer(instructions[i].decoded.opcode);
    if (!transfer || instructions[i].branch_target ||
        (i + 1 == instructions.size()
             ? *transfer != abc::ControlTransfer::kReturn &&
                   *transfer != abc::ControlTransfer::kThrow
             : *transfer != abc::ControlTransfer::kFallthrough))
      return {};
    if (std::holds_alternative<ReceiverCall>(
            LiftReceiverCall(instructions[i].decoded, frame_size))) {
      if (call) return {};
      call = i;
    }
  }
  if (call && MatchCopiedArgumentInvocation(instructions, *call, frame_size))
    return {};
  return call ? MatchStagedAt(body, frame_size, *call) : std::nullopt;
}
}  // namespace abcd::analysis
