#include "abcd/analysis/rest_arguments.h"

#include <climits>

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
std::variant<RestCopy, RestError> LiftRestArguments(
    const abc::DecodedInstruction& instruction) {
  std::uint64_t max = 0;
  switch (instruction.opcode) {
    case abc::Opcode::kCOPYRESTARGS_IMM8:
      max = UINT8_MAX;
      break;
    case abc::Opcode::kWIDE_COPYRESTARGS_PREF_IMM16:
      max = UINT16_MAX;
      break;
    default:
      return RestError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != 1 ||
      instruction.operands[0].kind != abc::OperandKind::kImmediate ||
      instruction.operands[0].width_bits !=
          (max == UINT8_MAX ? CHAR_BIT : 2 * CHAR_BIT) ||
      instruction.operands[0].raw_bits > max)
    return RestError::kMalformedInstruction;
  return RestCopy{static_cast<std::uint16_t>(instruction.operands[0].raw_bits)};
}

std::optional<RestPrologue> ProveRestPrologue(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::uint16_t source_length) {
  if (!frame.extra_arguments || frame.parameters !=
                                    static_cast<std::uint32_t>(source_length) + 1 ||
      frame.registers < frame.locals || !body.tries.empty() ||
      !body.handlers.empty())
    return std::nullopt;
  const auto copied = frame.registers - frame.locals;
  if (copied == 0 || copied > frame.locals ||
      body.instructions.size() < static_cast<std::size_t>(copied) + 2)
    return std::nullopt;
  for (std::uint32_t i = 0; i < copied; ++i) {
    const auto& instruction = body.instructions[i].decoded;
    if (instruction.opcode != abc::Opcode::kMOV_V4_V4 &&
        instruction.opcode != abc::Opcode::kMOV_V8_V8)
      return std::nullopt;
    const auto lifted = LiftScalarInstruction(instruction, frame.registers);
    const auto* assignment = std::get_if<ScalarAssignment>(&lifted);
    const auto* destination = assignment
                                  ? std::get_if<abc::RegisterId>(
                                        &assignment->destination)
                                  : nullptr;
    const auto* value = assignment
                            ? std::get_if<ScalarValue>(&assignment->expression)
                            : nullptr;
    const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    if (!destination || !source || destination->value != i ||
        source->value != frame.locals + i)
      return std::nullopt;
  }
  const auto copied_rest = abc::RegisterId{copied - 1};
  const auto rest = LiftRestArguments(body.instructions[copied].decoded);
  const auto* operation = std::get_if<RestCopy>(&rest);
  if (!operation || operation->first_argument != source_length)
    return std::nullopt;
  const auto store = LiftScalarInstruction(
      body.instructions[copied + 1].decoded, frame.registers);
  const auto* assignment = std::get_if<ScalarAssignment>(&store);
  const auto* destination = assignment
                                ? std::get_if<abc::RegisterId>(
                                      &assignment->destination)
                                : nullptr;
  const auto* value = assignment
                          ? std::get_if<ScalarValue>(&assignment->expression)
                          : nullptr;
  if (!destination || !value || *destination != copied_rest ||
      !std::holds_alternative<Accumulator>(*value))
    return std::nullopt;
  const auto original_rest = abc::RegisterId{
      frame.first_parameter.value + source_length};
  const auto prologue_end = body.instructions[copied + 1].offset.value;
  for (std::size_t i = copied + 2; i < body.instructions.size(); ++i) {
    // A second runtime copy allocates a distinct array; the source rest
    // binding can only stand for the single prologue copy.
    if (body.instructions[i].decoded.opcode == abc::Opcode::kCOPYRESTARGS_IMM8 ||
        body.instructions[i].decoded.opcode ==
            abc::Opcode::kWIDE_COPYRESTARGS_PREF_IMM16)
      return std::nullopt;
    if (body.instructions[i].branch_target &&
        body.instructions[i].branch_target->value <= prologue_end)
      return std::nullopt;
    const auto accesses = abc::GetRegisterAccesses(
        body.instructions[i].decoded, frame.registers);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
    if (!known) return std::nullopt;
    for (std::size_t j = 0; j < known->count; ++j) {
      const auto& window = known->windows[j];
      if (window.access != abc::OperandAccess::kWrite &&
          original_rest.value >= window.first.value &&
          original_rest.value - window.first.value < window.count)
        return std::nullopt;
    }
  }
  return RestPrologue{source_length, copied_rest};
}
}  // namespace abcd::analysis
