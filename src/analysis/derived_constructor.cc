#include "abcd/analysis/derived_constructor.h"

#include <new>
#include <stdexcept>
#include <vector>

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
namespace {
enum class OriginKind {
  kUnknown,
  kCurrentConstructor,
  kUninitializedThis,
  kSuperResult,
  kParameter
};
struct Origin {
  OriginKind kind = OriginKind::kUnknown;
  std::uint32_t parameter = 0;
};
bool CopyOpcode(abc::Opcode opcode) {
  switch (opcode) {
    case abc::Opcode::kMOV_V4_V4:
    case abc::Opcode::kMOV_V8_V8:
    case abc::Opcode::kMOV_V16_V16:
    case abc::Opcode::kLDA_V8:
    case abc::Opcode::kSTA_V8:
      return true;
    default:
      return false;
  }
}
}  // namespace

SuperInstruction LiftSuperInstruction(
    const abc::DecodedInstruction& instruction, std::uint32_t registers) {
  using abc::Opcode;
  if (instruction.opcode ==
      Opcode::kCALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8) {
    if (instruction.operand_count != 1 ||
        instruction.operands[0].kind != abc::OperandKind::kRegister ||
        instruction.operands[0].raw_bits > UINT8_MAX)
      return SuperError::kMalformedInstruction;
    if (instruction.operands[0].raw_bits >= registers)
      return SuperError::kRegisterRange;
    return SuperForward{{static_cast<std::uint32_t>(
        instruction.operands[0].raw_bits)}};
  }
  if (instruction.opcode == Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8 ||
      instruction.opcode == Opcode::kWIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8 ||
      instruction.opcode == Opcode::kSUPERCALLARROWRANGE_IMM8_IMM8_V8 ||
      instruction.opcode == Opcode::kWIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8) {
    const bool arrow = instruction.opcode == Opcode::kSUPERCALLARROWRANGE_IMM8_IMM8_V8 ||
        instruction.opcode == Opcode::kWIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8;
    const bool narrow = instruction.opcode == Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8 ||
        instruction.opcode == Opcode::kSUPERCALLARROWRANGE_IMM8_IMM8_V8;
    const std::size_t count_slot = narrow ? 1 : 0;
    const std::size_t first_slot = narrow ? 2 : 1;
    if (instruction.operand_count != (narrow ? 3 : 2) ||
        instruction.operands[count_slot].kind !=
            abc::OperandKind::kImmediate ||
        instruction.operands[count_slot].raw_bits >
            (narrow ? UINT8_MAX : UINT16_MAX) ||
        instruction.operands[first_slot].kind !=
            abc::OperandKind::kRegister ||
        instruction.operands[first_slot].raw_bits > UINT8_MAX ||
        (narrow &&
         (instruction.operands[0].kind != abc::OperandKind::kImmediate ||
          instruction.operands[0].raw_bits > UINT8_MAX)))
      return SuperError::kMalformedInstruction;
    const auto first = instruction.operands[first_slot].raw_bits;
    const auto count = instruction.operands[count_slot].raw_bits;
    if (first >= registers || count > registers - first)
      return SuperError::kRegisterRange;
    return SuperRange{
        {static_cast<std::uint32_t>(first)},
        static_cast<std::uint16_t>(count),
        narrow ? std::optional<std::uint16_t>{
                     static_cast<std::uint16_t>(
                         instruction.operands[0].raw_bits)}
               : std::nullopt,
        arrow ? SuperCalleeSource::kAccumulator : SuperCalleeSource::kInvocationFunction};
  }
  if (instruction.opcode == Opcode::kSUPERCALLSPREAD_IMM8_V8) {
    if (instruction.operand_count != 2 ||
        instruction.operands[0].kind != abc::OperandKind::kImmediate ||
        instruction.operands[0].raw_bits > UINT8_MAX ||
        instruction.operands[1].kind != abc::OperandKind::kRegister ||
        instruction.operands[1].raw_bits > UINT8_MAX)
      return SuperError::kMalformedInstruction;
    if (instruction.operands[1].raw_bits >= registers)
      return SuperError::kRegisterRange;
    return SuperSpread{
        {static_cast<std::uint32_t>(instruction.operands[1].raw_bits)},
        static_cast<std::uint8_t>(instruction.operands[0].raw_bits)};
  }
  std::uint64_t maximum;
  switch (instruction.opcode) {
    case Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8:
      maximum = UINT8_MAX;
      break;
    case Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM16:
      maximum = UINT16_MAX;
      break;
    default:
      return SuperError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != 1 ||
      instruction.operands[0].kind != abc::OperandKind::kImmediate ||
      instruction.operands[0].raw_bits > maximum)
    return SuperError::kMalformedInstruction;
  // RuntimeThrowIfSuperNotCorrectCall treats all other flag values as no-op.
  constexpr std::uint64_t kRequireInitializedFlag = 0;
  constexpr std::uint64_t kRequireUninitializedFlag = 1;
  const auto mode = instruction.operands[0].raw_bits;
  return SuperCheck{mode == kRequireInitializedFlag
                        ? SuperCheckKind::kRequireInitialized
                    : mode == kRequireUninitializedFlag
                        ? SuperCheckKind::kRequireUninitialized
                        : SuperCheckKind::kNoop};
}
DerivedConstructorResult ProveForwardingDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget) {
  using Error = DerivedConstructorError;
  if (!frame.function || !frame.new_target || !frame.this_value ||
      frame.locals > frame.registers ||
      frame.function->value < frame.locals ||
      frame.new_target->value < frame.locals ||
      frame.this_value->value < frame.locals ||
      frame.function->value >= frame.registers ||
      frame.new_target->value >= frame.registers ||
      frame.this_value->value >= frame.registers ||
      frame.function == frame.new_target ||
      frame.function == frame.this_value ||
      frame.new_target == frame.this_value ||
      frame.first_parameter.value > frame.registers ||
      frame.parameters > frame.registers - frame.first_parameter.value)
    return Error::kFrame;
  if (!body.tries.empty() || !body.handlers.empty())
    return Error::kControlFlow;
  if (frame.registers > budget.bytes / sizeof(Origin))
    return Error::kResourceLimit;
  try {
    std::vector<Origin> registers(frame.registers);
    if (registers.capacity() > budget.bytes / sizeof(Origin))
      return Error::kResourceLimit;
    registers[frame.function->value].kind =
        OriginKind::kCurrentConstructor;
    registers[frame.this_value->value].kind =
        OriginKind::kUninitializedThis;
    for (std::uint32_t parameter = 0; parameter < frame.parameters;
         ++parameter)
      registers[frame.first_parameter.value + parameter] =
          {OriginKind::kParameter, parameter};
    Origin accumulator;
    bool called = false;
    ForwardingDerivedConstructor forwarding;
    bool checked_uninitialized = false;
    bool checked_initialized = false;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto& instruction = body.instructions[i];
      if (instruction.branch_target) return Error::kControlFlow;
      const auto& decoded = instruction.decoded;
      if (decoded.opcode == abc::Opcode::kNOP) {
        if (decoded.operand_count != 0) return Error::kInstruction;
        continue;
      }
      if (decoded.opcode == abc::Opcode::kRETURN) {
        if (decoded.operand_count != 0 || i + 1 != body.instructions.size() ||
            !called || !checked_uninitialized || !checked_initialized ||
            accumulator.kind != OriginKind::kSuperResult)
          return Error::kReturn;
        return forwarding;
      }
      const auto super = LiftSuperInstruction(decoded, frame.registers);
      if (const auto* forward = std::get_if<SuperForward>(&super)) {
        if (called ||
            registers[forward->constructor.value].kind !=
                OriginKind::kCurrentConstructor)
          return Error::kValue;
        called = true;
        accumulator.kind = OriginKind::kSuperResult;
        continue;
      }
      if (const auto* range = std::get_if<SuperRange>(&super)) {
        if (range->callee != SuperCalleeSource::kInvocationFunction ||
            called || range->count > frame.parameters)
          return Error::kValue;
        for (std::uint32_t argument = 0; argument < range->count;
             ++argument) {
          const auto origin = registers[range->first.value + argument];
          if (origin.kind != OriginKind::kParameter ||
              origin.parameter != argument)
            return Error::kValue;
        }
        forwarding = {ForwardingKind::kParameters, range->count};
        called = true;
        accumulator.kind = OriginKind::kSuperResult;
        continue;
      }
      if (const auto* check = std::get_if<SuperCheck>(&super)) {
        switch (check->kind) {
          case SuperCheckKind::kRequireUninitialized:
            if (!called || checked_uninitialized || checked_initialized ||
                accumulator.kind != OriginKind::kUninitializedThis)
              return Error::kGuard;
            checked_uninitialized = true;
            break;
          case SuperCheckKind::kRequireInitialized:
            if (!checked_uninitialized || checked_initialized ||
                accumulator.kind != OriginKind::kSuperResult)
              return Error::kGuard;
            checked_initialized = true;
            break;
          case SuperCheckKind::kNoop:
            return Error::kGuard;
        }
        continue;
      }
      if (std::holds_alternative<SuperSpread>(super))
        return Error::kInstruction;
      if (std::get<SuperError>(super) != SuperError::kUnsupportedOpcode)
        return Error::kInstruction;
      if (!CopyOpcode(decoded.opcode)) return Error::kInstruction;
      const auto scalar = LiftScalarInstruction(decoded, frame.registers);
      const auto* assignment = std::get_if<ScalarAssignment>(&scalar);
      const auto* value = assignment
                              ? std::get_if<ScalarValue>(
                                    &assignment->expression)
                              : nullptr;
      if (!value || assignment->completion != CompletionEffect::kNormal)
        return Error::kInstruction;
      Origin source;
      if (std::holds_alternative<Accumulator>(*value))
        source = accumulator;
      else if (const auto* slot = std::get_if<abc::RegisterId>(value))
        source = registers[slot->value];
      else
        return Error::kInstruction;
      if (std::holds_alternative<Accumulator>(assignment->destination)) {
        accumulator = source;
      } else {
        const auto slot = std::get<abc::RegisterId>(assignment->destination);
        if (slot.value >= frame.locals) return Error::kValue;
        registers[slot.value] = source;
      }
    }
    return Error::kReturn;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
