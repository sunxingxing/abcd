#include "abcd/analysis/derived_constructor.h"

#include <new>
#include <stdexcept>
#include <vector>

#include "abcd/analysis/named_write.h"
#include "abcd/analysis/rest_arguments.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
namespace {
enum class OriginKind {
  kUnknown,
  kCurrentConstructor,
  kUninitializedThis,
  kRestArray,
  kSuperResult,
  kString
};
struct Origin {
  OriginKind kind = OriginKind::kUnknown;
  abc::PoolIndex string;
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

SpreadFieldConstructorResult ProveSpreadFieldDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget) {
  using Error = DerivedConstructorError;
  const auto rest = ProveRestPrologue(body, frame, 0);
  if (!rest || !frame.function || !frame.this_value || !frame.new_target ||
      frame.registers < frame.locals ||
      frame.function->value >= frame.registers ||
      frame.this_value->value >= frame.registers ||
      frame.new_target->value >= frame.registers ||
      frame.function->value < frame.locals ||
      frame.this_value->value < frame.locals ||
      frame.new_target->value < frame.locals ||
      frame.function == frame.this_value ||
      frame.function == frame.new_target ||
      frame.this_value == frame.new_target)
    return Error::kFrame;
  for (const auto& instruction : body.instructions)
    if (instruction.branch_target) return Error::kControlFlow;
  const auto copied = frame.registers - frame.locals;
  const auto this_local = frame.this_value->value - frame.locals;
  if (this_local >= copied || rest->local.value >= frame.locals)
    return Error::kFrame;
  if (frame.registers > budget.bytes / sizeof(Origin))
    return Error::kResourceLimit;
  const auto register_bytes =
      static_cast<std::size_t>(frame.registers) * sizeof(Origin);
  const auto field_budget =
      (budget.bytes - register_bytes) / sizeof(StringFieldWrite);
  std::size_t field_count = 0;
  for (const auto& instruction : body.instructions)
    if (instruction.decoded.opcode ==
            abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8 ||
        instruction.decoded.opcode ==
            abc::Opcode::kSTOBJBYNAME_IMM16_ID16_V8) {
      if (++field_count > field_budget) return Error::kResourceLimit;
    }
  // A compiler-generated default constructor uses the same rest/super/guard
  // protocol with no trailing fields. Zero fields does not relax that proof
  // or the source owner's separate lifetime parent-brand requirement.
  try {
    std::vector<Origin> registers(frame.registers);
    SpreadFieldDerivedConstructor proof;
    proof.writes.reserve(field_count);
    registers[frame.function->value].kind = OriginKind::kCurrentConstructor;
    registers[frame.this_value->value].kind = OriginKind::kUninitializedThis;
    for (std::uint32_t i = 0; i < copied; ++i)
      registers[i] = registers[frame.locals + i];
    registers[rest->local.value].kind = OriginKind::kRestArray;
    Origin accumulator;
    bool called = false;
    bool checked_uninitialized = false;
    bool bound = false;
    bool checked_initialized = false;
    for (std::size_t i = copied + 2; i < body.instructions.size(); ++i) {
      const auto& instruction = body.instructions[i];
      const auto& decoded = instruction.decoded;
      if (decoded.opcode == abc::Opcode::kNOP) {
        if (decoded.operand_count != 0) return Error::kInstruction;
        continue;
      }
      if (decoded.opcode == abc::Opcode::kRETURN) {
        if (decoded.operand_count != 0 || i + 1 != body.instructions.size() ||
            !called || !checked_uninitialized || !bound ||
            !checked_initialized || proof.writes.size() != field_count ||
            accumulator.kind != OriginKind::kSuperResult)
          return Error::kReturn;
        return proof;
      }
      const auto super = LiftSuperInstruction(decoded, frame.registers);
      if (const auto* spread = std::get_if<SuperSpread>(&super)) {
        if (called || accumulator.kind != OriginKind::kCurrentConstructor ||
            registers[spread->arguments.value].kind != OriginKind::kRestArray)
          return Error::kValue;
        called = true;
        accumulator.kind = OriginKind::kSuperResult;
        continue;
      }
      if (const auto* check = std::get_if<SuperCheck>(&super)) {
        switch (check->kind) {
          case SuperCheckKind::kRequireUninitialized:
            if (!called || checked_uninitialized || bound ||
                accumulator.kind != OriginKind::kUninitializedThis)
              return Error::kGuard;
            checked_uninitialized = true;
            break;
          case SuperCheckKind::kRequireInitialized:
            if (!bound || accumulator.kind != OriginKind::kSuperResult)
              return Error::kGuard;
            checked_initialized = true;
            break;
          case SuperCheckKind::kNoop:
            return Error::kGuard;
        }
        continue;
      }
      if (!std::holds_alternative<SuperError>(super) ||
          std::get<SuperError>(super) != SuperError::kUnsupportedOpcode)
        return Error::kInstruction;
      if (decoded.opcode == abc::Opcode::kLDA_STR_ID16) {
        if (!checked_initialized || decoded.operand_count != 1 ||
            decoded.operands[0].kind != abc::OperandKind::kString ||
            decoded.operands[0].raw_bits > UINT16_MAX)
          return Error::kInstruction;
        accumulator = {OriginKind::kString,
                       {static_cast<std::uint32_t>(
                           decoded.operands[0].raw_bits)}};
        continue;
      }
      const auto write = LiftNamedWrite(decoded, frame.registers);
      if (const auto* field = std::get_if<NamedWrite>(&write)) {
        if (!checked_initialized ||
            registers[field->object.value].kind != OriginKind::kSuperResult ||
            accumulator.kind != OriginKind::kString)
          return Error::kValue;
        proof.writes.push_back({field->name, accumulator.string});
        continue;
      }
      if (!std::holds_alternative<NamedWriteError>(write) ||
          std::get<NamedWriteError>(write) !=
              NamedWriteError::kUnsupportedOpcode)
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
        if (slot.value == this_local) {
          if (!checked_uninitialized || bound ||
              source.kind != OriginKind::kSuperResult)
            return Error::kValue;
          bound = true;
        }
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
