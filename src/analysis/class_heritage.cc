#include "abcd/analysis/class_heritage.h"

#include <algorithm>
#include <new>

#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/register_effect.h"

namespace abcd::analysis {
namespace {
struct Fact {
  std::size_t epoch = 0;
  HeritageKind kind = HeritageKind::kUnknown;
};
HeritageError Error(ScalarError error) {
  return error == ScalarError::kRegisterRange
             ? HeritageError::kFrameRange
             : HeritageError::kMalformedInstruction;
}
}  // namespace
std::variant<std::vector<ClassHeritage>, HeritageError> ProveClassHeritage(
    const abc::MethodBody& body, std::uint32_t registers,
    HeritageBudget budget) {
  std::size_t edges = body.handlers.size(), sites = 0;
  for (const auto& instruction : body.instructions) {
    if (instruction.branch_target) ++edges;
    const auto site = LiftClassCreation(instruction.decoded);
    if (const auto* creation = std::get_if<ClassCreation>(&site)) {
      if (creation->parent.value >= registers)
        return HeritageError::kFrameRange;
      ++sites;
    } else if (std::get<CreationError>(site) !=
               CreationError::kUnsupportedOpcode)
      return HeritageError::kMalformedInstruction;
  }
  auto remaining = budget.bytes;
  const auto admit = [&](std::size_t count, std::size_t size) {
    if (count > remaining / size) return false;
    remaining -= count * size;
    return true;
  };
  if (!admit(registers, sizeof(Fact)) || !admit(edges, sizeof(std::uint32_t)) ||
      !admit(sites, sizeof(ClassHeritage)))
    return HeritageError::kResourceLimit;
  try {
    std::vector<Fact> facts(registers);
    std::vector<std::uint32_t> targets;
    targets.reserve(edges);
    for (const auto& instruction : body.instructions)
      if (instruction.branch_target)
        targets.push_back(instruction.branch_target->value);
    for (const auto& handler : body.handlers)
      targets.push_back(handler.offset.value);
    std::sort(targets.begin(), targets.end());
    std::vector<ClassHeritage> result;
    result.reserve(sites);
    std::size_t epoch = 1, target = 0;
    HeritageKind acc = HeritageKind::kUnknown;
    const auto reset = [&] {
      ++epoch;
      acc = HeritageKind::kUnknown;
    };
    const auto read = [&](const ScalarValue& value) {
      if (std::holds_alternative<Accumulator>(value)) return acc;
      if (const auto* reg = std::get_if<abc::RegisterId>(&value)) {
        const auto& fact = facts[reg->value];
        return fact.epoch == epoch ? fact.kind : HeritageKind::kUnknown;
      }
      if (const auto* special = std::get_if<SpecialValue>(&value);
          special && *special == SpecialValue::kNull)
        return HeritageKind::kNull;
      return HeritageKind::kUnknown;
    };
    const auto assign = [&](const ScalarSlot& slot, HeritageKind kind) {
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
        facts[reg->value] = {epoch, kind};
      else
        acc = kind;
    };
    for (const auto& instruction : body.instructions) {
      if (target < targets.size() &&
          targets[target] <= instruction.offset.value) {
        reset();
        while (target < targets.size() &&
               targets[target] <= instruction.offset.value)
          ++target;
      }
      const auto site = LiftClassCreation(instruction.decoded);
      if (const auto* creation = std::get_if<ClassCreation>(&site)) {
        result.push_back(
            {instruction.offset, read(ScalarValue{creation->parent})});
        // Creation may invoke user code but cannot mutate caller-private slots.
        acc = HeritageKind::kUnknown;
        continue;
      }
      const auto transfer = abc::GetControlTransfer(instruction.decoded.opcode);
      if (!transfer || *transfer != abc::ControlTransfer::kFallthrough) {
        reset();
        continue;
      }
      if (instruction.decoded.opcode == abc::Opcode::kLDHOLE) {
        if (instruction.decoded.operand_count != 0)
          return HeritageError::kMalformedInstruction;
        acc = HeritageKind::kAbsent;
        continue;
      }
      const auto scalar = LiftScalarInstruction(instruction.decoded, registers);
      if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar)) {
        const auto* value = std::get_if<ScalarValue>(&assignment->expression);
        assign(assignment->destination,
               value ? read(*value) : HeritageKind::kUnknown);
        continue;
      }
      if (const auto error = std::get<ScalarError>(scalar);
          error != ScalarError::kUnsupportedOpcode)
        return Error(error);
      const auto effect = LiftRegisterEffect(instruction.decoded, registers);
      if (const auto* failure = std::get_if<ScalarError>(&effect)) {
        if (*failure != ScalarError::kUnsupportedOpcode) return Error(*failure);
        reset();
      } else if (const auto& write = std::get<RegisterEffect>(effect).write)
        assign(*write, HeritageKind::kUnknown);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return HeritageError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
