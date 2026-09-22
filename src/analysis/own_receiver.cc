#include "abcd/analysis/own_receiver.h"

#include <algorithm>
#include <new>

#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/register_effect.h"

namespace abcd::analysis {
namespace {
ReceiverError Error(OwnWriteError error) {
  return error == OwnWriteError::kRegisterRange
             ? ReceiverError::kFrameRange
             : ReceiverError::kMalformedInstruction;
}
ReceiverError Error(ScalarError error) {
  return error == ScalarError::kRegisterRange
             ? ReceiverError::kFrameRange
             : ReceiverError::kMalformedInstruction;
}
}  // namespace
std::variant<std::vector<OwnReceiver>, ReceiverError> ProveOwnReceivers(
    const abc::MethodBody& body, std::uint32_t registers,
    std::optional<abc::RegisterId> entry_this, ReceiverBudget budget) {
  if (entry_this && entry_this->value >= registers)
    return ReceiverError::kFrameRange;
  std::uint32_t stop = UINT32_MAX;
  std::size_t count = 0;
  for (const auto& instruction : body.instructions) {
    if (instruction.branch_target)
      stop = std::min(stop, instruction.branch_target->value);
    const auto write = LiftOwnWrite(instruction.decoded, registers);
    if (std::holds_alternative<OwnWrite>(write))
      ++count;
    else if (const auto error = std::get<OwnWriteError>(write);
             error != OwnWriteError::kUnsupportedOpcode)
      return Error(error);
  }
  for (const auto& handler : body.handlers)
    stop = std::min(stop, handler.offset.value);
  if (registers > budget.bytes ||
      count > (budget.bytes - registers) / sizeof(OwnReceiver))
    return ReceiverError::kResourceLimit;
  try {
    std::vector<ReceiverOrigin> aliases(registers, ReceiverOrigin::kUnknown);
    if (entry_this) aliases[entry_this->value] = ReceiverOrigin::kEntryThis;
    ReceiverOrigin accumulator = ReceiverOrigin::kUnknown;
    std::vector<OwnReceiver> result;
    result.reserve(count);
    bool active = true;
    const auto read = [&](const ScalarValue& value) -> ReceiverOrigin {
      if (std::holds_alternative<Accumulator>(value)) return accumulator;
      if (const auto* reg = std::get_if<abc::RegisterId>(&value))
        return aliases[reg->value];
      return ReceiverOrigin::kUnknown;
    };
    const auto assign = [&](const ScalarSlot& slot, ReceiverOrigin value) {
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
        aliases[reg->value] = value;
      else
        accumulator = value;
    };
    for (const auto& instruction : body.instructions) {
      if (instruction.offset.value >= stop) active = false;
      const auto own = LiftOwnWrite(instruction.decoded, registers);
      if (const auto* write = std::get_if<OwnWrite>(&own)) {
        result.push_back(
            {instruction.offset, active && aliases[write->object.value] ==
                                               ReceiverOrigin::kEntryThis
                                     ? ReceiverOrigin::kEntryThis
                                     : ReceiverOrigin::kUnknown});
        continue;
      }
      if (!active) continue;
      const auto transfer = abc::GetControlTransfer(instruction.decoded.opcode);
      if (!transfer || *transfer != abc::ControlTransfer::kFallthrough) {
        active = false;
        continue;
      }
      if (instruction.decoded.opcode == abc::Opcode::kLDTHIS) {
        if (instruction.decoded.operand_count != 0)
          return ReceiverError::kMalformedInstruction;
        // v7.0 EcmaInterpreter/InterpreterAssembly::GetThis reads
        // frame.thisObj, independent of the optional, writable ABI register
        // copy. Unsupported super/resume operations end this prefix before
        // changing it.
        accumulator = ReceiverOrigin::kEntryThis;
        continue;
      }
      const auto scalar = LiftScalarInstruction(instruction.decoded, registers);
      if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar)) {
        const auto* value = std::get_if<ScalarValue>(&assignment->expression);
        assign(assignment->destination,
               value ? read(*value) : ReceiverOrigin::kUnknown);
        continue;
      }
      const auto effect = LiftRegisterEffect(instruction.decoded, registers);
      if (const auto* error = std::get_if<ScalarError>(&effect)) {
        if (*error != ScalarError::kUnsupportedOpcode) return Error(*error);
        // Never treat unknown instructions (notably super/resume/eval) as NOPs.
        active = false;
      } else if (const auto& write = std::get<RegisterEffect>(effect).write)
        assign(*write, ReceiverOrigin::kUnknown);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return ReceiverError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
