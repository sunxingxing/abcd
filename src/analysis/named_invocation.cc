#include "abcd/analysis/named_invocation.h"

#include <algorithm>
#include <array>
#include <new>

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
  // CALLTHIS*WITHNAME (API 24+) has CALLTHIS* semantics in the pinned v7
  // interpreter (ets_runtime interpreter-inl.cpp:1402-1460): the string_id only
  // names the callee in the TypeError raised for a non-callable value, which
  // the source contract does not preserve for CALLTHIS* either. The name
  // operand is validated and then ignored.
  const bool wide_named=instruction.opcode==abc::Opcode::kWIDE_CALLTHISRANGEWITHNAME_PREF_IMM16_ID16_V8;
  if(wide_named||instruction.opcode==abc::Opcode::kWIDE_CALLTHISRANGE_PREF_IMM16_V8){
    const auto receiver=wide_named?2u:1u;
    if(instruction.operand_count!=receiver+1||instruction.operands[0].kind!=abc::OperandKind::kImmediate||instruction.operands[0].raw_bits>UINT16_MAX||
       (wide_named&&(instruction.operands[1].kind!=abc::OperandKind::kString||instruction.operands[1].raw_bits>UINT16_MAX))||
       instruction.operands[receiver].kind!=abc::OperandKind::kRegister||instruction.operands[receiver].raw_bits>UINT8_MAX)return CallError::kMalformedInstruction;
    const auto count=instruction.operands[0].raw_bits,base=instruction.operands[receiver].raw_bits;
    if(base>=frame_size||count>frame_size-base-1)return CallError::kRegisterRange;
    return ReceiverCall{{static_cast<std::uint32_t>(base)},CallRange{{static_cast<std::uint32_t>(base+1)},static_cast<std::uint16_t>(count)}};
  }
  std::uint8_t count = 0;
  bool named = false;
  CallArgumentForm form = CallArgumentForm::kRegisters;
  switch (instruction.opcode) {
    case abc::Opcode::kAPPLY_IMM8_V8_V8:
      form = CallArgumentForm::kApplyArray; count = 1; break;
    case abc::Opcode::kCALLTHIS0WITHNAME_IMM8_ID16_V8:
      named = true;
      [[fallthrough]];
    // interpreter-inl.cpp:1499 CALLRUNTIME_CALLINIT: "same as callthis0"
    // (accumulator callee, explicit this, no arguments, result in acc).
    case abc::Opcode::kCALLRUNTIME_CALLINIT_PREF_IMM8_V8:
    case abc::Opcode::kCALLTHIS0_IMM8_V8:
      break;
    case abc::Opcode::kCALLTHIS1WITHNAME_IMM8_ID16_V8_V8:
      named = true;
      [[fallthrough]];
    case abc::Opcode::kCALLTHIS1_IMM8_V8_V8:
      count = 1;
      break;
    case abc::Opcode::kCALLTHIS2WITHNAME_IMM8_ID16_V8_V8_V8:
      named = true;
      [[fallthrough]];
    case abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8:
      count = 2;
      break;
    case abc::Opcode::kCALLTHIS3WITHNAME_IMM8_ID16_V8_V8_V8_V8:
      named = true;
      [[fallthrough]];
    case abc::Opcode::kCALLTHIS3_IMM8_V8_V8_V8_V8:
      count = 3;
      break;
    case abc::Opcode::kCALLTHISRANGEWITHNAME_IMM8_IMM8_ID16_V8:
      named = true;
      [[fallthrough]];
    case abc::Opcode::kCALLTHISRANGE_IMM8_IMM8_V8: {
      constexpr std::size_t kRangeCount = 1;
      const std::size_t name = named ? 2 : 0;
      const std::size_t receiver_index = named ? 3 : 2;
      if (instruction.operand_count != (named ? 4u : 3u) ||
          instruction.operands[0].kind != abc::OperandKind::kImmediate ||
          instruction.operands[0].raw_bits > UINT8_MAX ||
          instruction.operands[kRangeCount].kind !=
              abc::OperandKind::kImmediate ||
          instruction.operands[kRangeCount].raw_bits > UINT8_MAX ||
          (named && instruction.operands[name].kind != abc::OperandKind::kString) ||
          instruction.operands[receiver_index].kind !=
              abc::OperandKind::kRegister ||
          instruction.operands[receiver_index].raw_bits > UINT8_MAX)
        return CallError::kMalformedInstruction;
      const auto base = instruction.operands[receiver_index].raw_bits;
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
  constexpr std::size_t kName = 1;
  const std::size_t receiver_index = named ? 2 : 1;
  const std::size_t first_argument = receiver_index + 1;
  if (instruction.operand_count != first_argument + count ||
      instruction.operands[kCache].kind != abc::OperandKind::kImmediate ||
      instruction.operands[kCache].raw_bits > UINT8_MAX ||
      (named && instruction.operands[kName].kind != abc::OperandKind::kString))
    return CallError::kMalformedInstruction;
  ReceiverCall result{};
  CallRegisters arguments;
  arguments.count = count;
  for (std::size_t i = receiver_index; i < first_argument + count; ++i) {
    const auto& operand = instruction.operands[i];
    if (operand.kind != abc::OperandKind::kRegister ||
        operand.raw_bits > UINT8_MAX)
      return CallError::kMalformedInstruction;
    if (operand.raw_bits >= frame_size) return CallError::kRegisterRange;
    const abc::RegisterId reg{static_cast<std::uint32_t>(operand.raw_bits)};
    if (i == receiver_index)
      result.receiver = reg;
    else
      arguments.values[i - first_argument] = reg;
  }
  result.arguments = arguments;
  result.form = form;
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
    if (call->form != CallArgumentForm::kRegisters || call->receiver != *receiver) return {};
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
  if (!call || call->form != CallArgumentForm::kRegisters || call->receiver != *receiver) return {};
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
  if (!call_value || call_value->form != CallArgumentForm::kRegisters || call_value->Count() != 1 ||
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
std::optional<StagedNamedInvocation> MatchStagedShape(
    const abc::MethodBody& body, std::uint64_t frame_size,
    std::size_t call_index, std::size_t spill, abc::RegisterId callee,
    const ReceiverCall& invoke) {
  const auto& instructions = body.instructions;
  using abc::Opcode;
  if (invoke.form != CallArgumentForm::kRegisters ||
      call_index < 4 || call_index >= instructions.size() || spill < 2)
    return {};
  const auto reload = call_index - 1;
  const auto loaded =
      LiftScalarInstruction(instructions[reload].decoded, frame_size);
  const auto* load = std::get_if<ScalarAssignment>(&loaded);
  const auto* load_value =
      load ? std::get_if<ScalarValue>(&load->expression) : nullptr;
  const auto* loaded_callee =
      load_value ? std::get_if<abc::RegisterId>(load_value) : nullptr;
  if (!load || !loaded_callee || *loaded_callee != callee ||
      !std::holds_alternative<Accumulator>(load->destination) ||
      callee == invoke.receiver)
    return {};
  for (std::uint32_t i = 0; i < invoke.Count(); ++i)
    if (callee == *invoke.At(i)) return {};
  if (invoke.Count() == 0 ? spill + 1 != reload : spill + 2 >= reload)
    return {};
  const auto stored =
      LiftScalarInstruction(instructions[spill].decoded, frame_size);
  const auto* store = std::get_if<ScalarAssignment>(&stored);
  const auto* store_value =
      store ? std::get_if<ScalarValue>(&store->expression) : nullptr;
  const auto* destination =
      store ? std::get_if<abc::RegisterId>(&store->destination) : nullptr;
  if (!store || !store_value || !destination || *destination != callee ||
      !std::holds_alternative<Accumulator>(*store_value) ||
      instructions[spill].decoded.opcode != Opcode::kSTA_V8)
    return {};
  const auto property = spill - 1;
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
  if (!copy || !receiver || *receiver != invoke.receiver ||
      !std::holds_alternative<Accumulator>(copy->destination))
    return {};
  if (invoke.Count() != 0) {
    const auto overwrite = LiftRegisterEffect(
        instructions[spill + 1].decoded, frame_size);
    const auto* effect = std::get_if<RegisterEffect>(&overwrite);
    if (!effect || !effect->write ||
        !std::holds_alternative<Accumulator>(*effect->write))
      return {};
    for (std::size_t read_index = 0; read_index < effect->read_count;
         ++read_index)
      if (std::holds_alternative<Accumulator>(effect->reads[read_index]))
        return {};
  }
  return StagedNamedInvocation{property, spill, reload, call_index, *read,
                               invoke, callee,
                               invoke.Count() == 0
                                   ? SpilledCallTiming::kImmediate
                                   : SpilledCallTiming::kDeferredArgument};
}

std::optional<StagedNamedInvocation> MatchStagedAt(
    const abc::MethodBody& body, std::uint64_t frame_size,
    std::size_t call_index) {
  const auto& instructions = body.instructions;
  if (call_index < 5 || call_index >= instructions.size()) return {};
  const auto lifted =
      LiftReceiverCall(instructions[call_index].decoded, frame_size);
  const auto* invoke = std::get_if<ReceiverCall>(&lifted);
  if (!invoke || invoke->form != CallArgumentForm::kRegisters || invoke->Count() == 0) return {};
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
      if (access.access != abc::OperandAccess::kWrite)
        return {};
      writes = true;
    }
    if (writes) {
      spill = i;
      break;
    }
  }
  if (!spill) return {};
  auto matched = MatchStagedShape(body, frame_size, call_index, *spill,
                                  *callee, *invoke);
  if (!matched) return {};
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
    if (killed) return matched;
  }
  return matched;
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

StagedMatchResult MatchStagedNamedInvocations(
    const abc::MethodBody& body, std::uint64_t frame_size,
    std::size_t bytes, std::uint64_t work_limit) {
  using abc::OperandAccess;
  using abc::RegisterAccesses;
  constexpr std::size_t kTrackedRegisters = UINT8_MAX + std::size_t{1};
  constexpr std::size_t kCandidateBytes =
      sizeof(StagedNamedInvocation) + sizeof(std::uint8_t);
  enum class ReadsSinceWrite : std::uint8_t { kNone, kOne, kMany };
  StagedMatches result;
  auto& matches = result.calls;
  std::uint64_t work = 0;
  const auto charge = [&](std::uint64_t units = 1) {
    if (units > work_limit - work) return false;
    work += units;
    return true;
  };
  const auto done = [&]() -> StagedMatchResult {
    result.work_units = work;
    return std::move(result);
  };
  const auto& instructions = body.instructions;
  if (!body.tries.empty() || !body.handlers.empty() || instructions.empty())
    return done();
  std::size_t receiver_calls = 0;
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    if (!charge()) return StagedMatchError::kWorkLimit;
    const auto transfer =
        abc::GetControlTransfer(instructions[i].decoded.opcode);
    if (!transfer || instructions[i].branch_target ||
        (i + 1 == instructions.size()
             ? *transfer != abc::ControlTransfer::kReturn &&
                   *transfer != abc::ControlTransfer::kThrow
             : *transfer != abc::ControlTransfer::kFallthrough))
      return done();
    receiver_calls += std::holds_alternative<ReceiverCall>(
        LiftReceiverCall(instructions[i].decoded, frame_size));
  }
  if (receiver_calls == 0) return done();
  if (receiver_calls > bytes / kCandidateBytes)
    return StagedMatchError::kResourceLimit;
  try {
    matches.reserve(receiver_calls);
    std::array<std::optional<std::size_t>, kTrackedRegisters> last_write{};
    std::array<ReadsSinceWrite, kTrackedRegisters> reads{};
    const auto visit = [&](const RegisterAccesses& accesses,
                           const auto& action) {
      for (std::size_t window = 0; window < accesses.count; ++window) {
        if (!charge()) return false;
        const auto& access = accesses.windows[window];
        if (access.first.value >= kTrackedRegisters) continue;
        const auto end = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(access.first.value) + access.count,
            kTrackedRegisters);
        for (std::uint32_t reg = access.first.value; reg < end; ++reg) {
          if (!charge()) return false;
          action(reg, access.access);
        }
      }
      return true;
    };
    for (std::size_t i = 0; i < instructions.size(); ++i) {
      if (!charge()) return StagedMatchError::kWorkLimit;
      const auto called = LiftReceiverCall(instructions[i].decoded, frame_size);
      const auto* invoke = std::get_if<ReceiverCall>(&called);
      if (invoke && i > 0) {
        if (!charge(1 + invoke->Count()))
          return StagedMatchError::kWorkLimit;
        const auto loaded =
            LiftScalarInstruction(instructions[i - 1].decoded, frame_size);
        const auto* load = std::get_if<ScalarAssignment>(&loaded);
        const auto* value =
            load ? std::get_if<ScalarValue>(&load->expression) : nullptr;
        const auto* callee =
            value ? std::get_if<abc::RegisterId>(value) : nullptr;
        if (load && callee && callee->value < kTrackedRegisters &&
            invoke->receiver.value < kTrackedRegisters &&
            std::holds_alternative<Accumulator>(load->destination) &&
            last_write[callee->value] &&
            reads[callee->value] == ReadsSinceWrite::kOne &&
            (!last_write[invoke->receiver.value] ||
             *last_write[invoke->receiver.value] <=
                 *last_write[callee->value]) &&
            !MatchCopiedArgumentInvocation(instructions, i, frame_size)) {
          auto shape = MatchStagedShape(
              body, frame_size, i, *last_write[callee->value], *callee,
              *invoke);
          if (shape) {
            // A deferred argument may contain immediate zero-argument calls.
            // Its named getter still precedes them; only disjoint or this
            // exact nesting is admitted by the source writer.
            for (std::size_t prior = matches.size(); prior != 0;) {
              const auto& inside = matches[--prior];
              if (inside.call < shape->property) break;
              if (shape->timing != SpilledCallTiming::kDeferredArgument ||
                  inside.timing != SpilledCallTiming::kImmediate ||
                  inside.property <= shape->spill ||
                  inside.call >= shape->reload) {
                matches.clear();
                return done();
              }
            }
            matches.push_back(*shape);
          }
        }
      }
      const auto accesses =
          abc::GetRegisterAccesses(instructions[i].decoded, frame_size);
      const auto* known = std::get_if<RegisterAccesses>(&accesses);
      if (!known) {
        matches.clear();
        return done();
      }
      if (!visit(*known, [&](std::uint32_t reg, OperandAccess access) {
        if (access != OperandAccess::kWrite)
          reads[reg] = reads[reg] == ReadsSinceWrite::kNone
                           ? ReadsSinceWrite::kOne
                           : ReadsSinceWrite::kMany;
        if (access != OperandAccess::kRead) {
          last_write[reg] = i;
          reads[reg] = access == OperandAccess::kReadWrite
                           ? ReadsSinceWrite::kMany
                           : ReadsSinceWrite::kNone;
        }
      })) return StagedMatchError::kWorkLimit;
    }
    if (matches.empty()) return done();
    std::vector<std::uint8_t> live(matches.size(), 1);
    std::array<std::optional<std::size_t>, kTrackedRegisters> next_read{};
    std::array<std::optional<std::size_t>, kTrackedRegisters> next_write{};
    std::size_t candidate = matches.size();
    for (std::size_t i = instructions.size(); i-- > 0;) {
      if (!charge()) return StagedMatchError::kWorkLimit;
      if (candidate != 0 && matches[candidate - 1].call == i) {
        const auto reg = matches[candidate - 1].callee.value;
        if (next_read[reg] &&
            (!next_write[reg] || *next_read[reg] <= *next_write[reg]))
          live[candidate - 1] = 0;
        --candidate;
      }
      const auto accesses =
          abc::GetRegisterAccesses(instructions[i].decoded, frame_size);
      const auto* known = std::get_if<RegisterAccesses>(&accesses);
      if (!known) {
        matches.clear();
        return done();
      }
      if (!visit(*known, [&](std::uint32_t reg, OperandAccess access) {
        if (access != OperandAccess::kWrite) next_read[reg] = i;
        if (access != OperandAccess::kRead) next_write[reg] = i;
      })) return StagedMatchError::kWorkLimit;
    }
    if (!charge(matches.size())) return StagedMatchError::kWorkLimit;
    std::size_t retained = 0;
    for (std::size_t i = 0; i < matches.size(); ++i)
      if (live[i]) matches[retained++] = matches[i];
    matches.resize(retained);
    return done();
  } catch (const std::bad_alloc&) {
    return StagedMatchError::kResourceLimit;
  } catch (const std::length_error&) {
    return StagedMatchError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
