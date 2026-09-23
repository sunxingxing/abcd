#include "abcd/analysis/register_effect.h"

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/indexed_read.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/own_write.h"

namespace abcd::analysis {
namespace {
using EffectResult = std::variant<RegisterEffect, ScalarError>;
void Read(RegisterEffect& effect, const ScalarValue& value) {
  if (std::holds_alternative<Accumulator>(value))
    effect.reads[effect.read_count++] = Accumulator{};
  else if (const auto* reg = std::get_if<abc::RegisterId>(&value))
    effect.reads[effect.read_count++] = *reg;
}
void Read(RegisterEffect& effect, const UnaryExpression& value) {
  Read(effect, value.input);
}
void Read(RegisterEffect& effect, const BinaryExpression& value) {
  Read(effect, value.left);
  Read(effect, value.right);
}
void Read(RegisterEffect& effect, const PropertyRead& value) {
  Read(effect, value.receiver);
  Read(effect, value.key);
}
RegisterEffect EffectOf(const ScalarAssignment& value) {
  RegisterEffect effect;
  std::visit([&](const auto& expression) { Read(effect, expression); },
             value.expression);
  effect.write = value.destination;
  return effect;
}
RegisterEffect EffectOf(const UnaryExpression& value) {
  RegisterEffect effect;
  Read(effect, value);
  return effect;
}
RegisterEffect EffectOf(const NamedRead&) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = Accumulator{};
  effect.write = Accumulator{};
  return effect;
}
RegisterEffect EffectOf(const NamedWrite& value) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = value.object;
  effect.reads[effect.read_count++] = Accumulator{};
  return effect;
}
RegisterEffect EffectOf(const OwnWrite& value) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = value.object;
  effect.reads[effect.read_count++] = Accumulator{};
  return effect;
}
RegisterEffect EffectOf(const IndexedRead& value) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = value.receiver;
  effect.write = Accumulator{};
  return effect;
}
RegisterEffect EffectOf(const OrdinaryCall& value) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = Accumulator{};
  effect.write = Accumulator{};
  if (const auto* range = std::get_if<CallRange>(&value.arguments)) {
    effect.range = *range;
  } else {
    const auto& fixed = std::get<CallRegisters>(value.arguments);
    for (std::size_t i = 0; i < fixed.count; ++i)
      effect.reads[effect.read_count++] = fixed.values[i];
  }
  return effect;
}
RegisterEffect EffectOf(const LexicalStore& value) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = value.value;
  return effect;
}
RegisterEffect EffectOf(const PopEnvironment&) { return {}; }
RegisterEffect AccumulatorDefinition() {
  RegisterEffect effect;
  effect.write = Accumulator{};
  return effect;
}
RegisterEffect EffectOf(const ModuleLoad&) {
  // A module binding may be Hole, but its normal return only replaces acc.
  // Private caller registers remain valid across the possibly throwing load.
  return AccumulatorDefinition();
}
RegisterEffect EffectOf(const LexicalLoad&) { return AccumulatorDefinition(); }
RegisterEffect EffectOf(const PushEnvironment&) {
  return AccumulatorDefinition();
}
RegisterEffect EffectOf(const CurrentEnvironment&) {
  return AccumulatorDefinition();
}
RegisterEffect EffectOf(const LoadHole&) { return AccumulatorDefinition(); }
RegisterEffect EffectOf(const HoleGuard& value) {
  RegisterEffect effect;
  effect.reads[effect.read_count++] = value.value;
  if (const auto* name = std::get_if<abc::RegisterId>(&value.name))
    effect.reads[effect.read_count++] = *name;
  return effect;
}
RegisterEffect EffectOf(const FunctionCreation& value) {
  auto effect = AccumulatorDefinition();
  if (value.home_object == HomeObjectInput::kAccumulator)
    effect.reads[effect.read_count++] = Accumulator{};
  return effect;
}
RegisterEffect EffectOf(const BufferedObjectCreation&) {
  return AccumulatorDefinition();
}
RegisterEffect EffectOf(const EmptyObjectCreation&) {
  return AccumulatorDefinition();
}
template <class Error>
ScalarError EffectOf(Error error) {
  if (error == Error::kUnsupportedOpcode)
    return ScalarError::kUnsupportedOpcode;
  if constexpr (requires { Error::kRegisterRange; }) {
    if (error == Error::kRegisterRange) return ScalarError::kRegisterRange;
  }
  return ScalarError::kMalformedInstruction;
}
template <class... T>
EffectResult Adapt(const std::variant<T...>& lifted) {
  return std::visit(
      [](const auto& value) -> EffectResult { return EffectOf(value); },
      lifted);
}
bool Unsupported(const EffectResult& result) {
  const auto* error = std::get_if<ScalarError>(&result);
  return error && *error == ScalarError::kUnsupportedOpcode;
}
}  // namespace

EffectResult LiftRegisterEffect(const abc::DecodedInstruction& instruction,
                                std::uint64_t frame_size) {
  // Reuse reviewed semantic lifts, never ISA may-write flags as definite defs.
  // Non-unsupported failures must survive this dispatch unchanged.
  if (auto result = Adapt(LiftScalarInstruction(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftBranchCondition(instruction));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftNamedRead(instruction)); !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftNamedWrite(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftOwnWrite(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftIndexedRead(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftOrdinaryCall(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftLexicalInstruction(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftModuleLoad(instruction)); !Unsupported(result))
    return result;
  const auto global = LiftGlobalAccess(instruction);
  if (const auto* access = std::get_if<GlobalAccess>(&global)) {
    if (access->operation == GlobalOp::kReadName)
      return AccumulatorDefinition();
  } else if (std::get<GlobalAccessError>(global) ==
             GlobalAccessError::kMalformedInstruction) {
    return ScalarError::kMalformedInstruction;
  }
  if (auto result = Adapt(LiftHoleInstruction(instruction, frame_size));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftFunctionCreation(instruction));
      !Unsupported(result))
    return result;
  if (auto result = Adapt(LiftObjectCreation(instruction));
      !Unsupported(result))
    return result;
  using abc::Opcode;
  switch (instruction.opcode) {
    case Opcode::kLDA_STR_ID16:
      if (instruction.operand_count != 1 ||
          instruction.operands[0].kind != abc::OperandKind::kString ||
          instruction.operands[0].raw_bits > UINT16_MAX)
        return ScalarError::kMalformedInstruction;
      return AccumulatorDefinition();
    case Opcode::kJMP_IMM8:
    case Opcode::kJMP_IMM16:
    case Opcode::kJMP_IMM32:
      if (instruction.operand_count != 1 ||
          instruction.operands[0].kind != abc::OperandKind::kImmediate ||
          !instruction.branch_delta || !instruction.jump ||
          instruction.conditional)
        return ScalarError::kMalformedInstruction;
      return RegisterEffect{};
    case Opcode::kNOP:
    case Opcode::kRETURNUNDEFINED:
    case Opcode::kRETURN:
    case Opcode::kTHROW_PREF_NONE: {
      if (instruction.operand_count != 0)
        return ScalarError::kMalformedInstruction;
      RegisterEffect effect;
      if (instruction.opcode == Opcode::kRETURN ||
          instruction.opcode == Opcode::kTHROW_PREF_NONE)
        effect.reads[effect.read_count++] = Accumulator{};
      return effect;
    }
    default:
      return ScalarError::kUnsupportedOpcode;
  }
}
}  // namespace abcd::analysis
