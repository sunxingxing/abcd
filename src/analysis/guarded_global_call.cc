#include "abcd/analysis/guarded_global_call.h"

#include <array>

#include "abcd/abc/register_access.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
namespace {
enum class Step : std::size_t {
  kKey,
  kStoreKey,
  kReceiverGlobal,
  kStoreReceiver,
  kLoadReceiver,
  kReceiverProperty,
  kIn,
  kToBoolean,
  kBranchFalse,
  kTrue,
  kJumpJoin,
  kFalse,
  kInvert,
  kSkipCall,
  kCallGlobal,
  kStoreCallGlobal,
  kLoadCallGlobal,
  kCallProperty,
  kStoreMethod,
  kArgumentGlobal,
  kStoreArgumentGlobal,
  kLoadArgumentGlobal,
  kArgumentProperty,
  kStoreObject,
  kArgumentKey,
  kStoreArgumentKey,
  kFunction,
  kStoreFunction,
  kLoadMethod,
  kCall,
  kAfter
};
constexpr std::size_t Offset(Step step) {
  return static_cast<std::size_t>(step);
}
const abc::BodyInstruction& At(const abc::MethodBody& body,
                               std::size_t first, Step step) {
  return body.instructions[first + Offset(step)];
}
std::optional<abc::PoolIndex> String(const abc::BodyInstruction& site) {
  const auto& instruction = site.decoded;
  if (instruction.opcode != abc::Opcode::kLDA_STR_ID16 ||
      instruction.operand_count != 1 ||
      instruction.operands[0].kind != abc::OperandKind::kString ||
      instruction.operands[0].raw_bits > UINT16_MAX)
    return {};
  return abc::PoolIndex{
      static_cast<std::uint32_t>(instruction.operands[0].raw_bits)};
}
std::optional<abc::RegisterId> Register(const abc::BodyInstruction& site,
                                        abc::Opcode opcode,
                                        std::uint64_t frame_size) {
  const auto& instruction = site.decoded;
  if (instruction.opcode != opcode || instruction.operand_count != 1 ||
      instruction.operands[0].kind != abc::OperandKind::kRegister ||
      instruction.operands[0].raw_bits >= frame_size ||
      instruction.operands[0].raw_bits > UINT8_MAX)
    return {};
  return abc::RegisterId{
      static_cast<std::uint32_t>(instruction.operands[0].raw_bits)};
}
std::optional<abc::PoolIndex> Global(const abc::BodyInstruction& site) {
  const auto result = LiftGlobalAccess(site.decoded);
  const auto* access = std::get_if<GlobalAccess>(&result);
  if (!access || access->operation != GlobalOp::kReadName) return {};
  return access->name;
}
std::optional<abc::PoolIndex> Property(const abc::BodyInstruction& site) {
  const auto result = LiftNamedRead(site.decoded);
  const auto* read = std::get_if<NamedRead>(&result);
  if (!read) return {};
  return read->name;
}
bool Constant(const abc::BodyInstruction& site, abc::Opcode opcode) {
  return site.decoded.opcode == opcode && site.decoded.operand_count == 0;
}
bool Unary(const abc::BodyInstruction& site, UnaryOperator op,
           std::uint64_t frame_size) {
  const auto result = LiftScalarInstruction(site.decoded, frame_size);
  const auto* assignment = std::get_if<ScalarAssignment>(&result);
  if (!assignment ||
      !std::holds_alternative<Accumulator>(assignment->destination))
    return false;
  const auto* expression =
      std::get_if<UnaryExpression>(&assignment->expression);
  return expression && expression->op == op &&
         std::holds_alternative<Accumulator>(expression->input);
}
bool In(const abc::BodyInstruction& site, abc::RegisterId key,
        std::uint64_t frame_size) {
  const auto result = LiftScalarInstruction(site.decoded, frame_size);
  const auto* assignment = std::get_if<ScalarAssignment>(&result);
  if (!assignment ||
      !std::holds_alternative<Accumulator>(assignment->destination))
    return false;
  const auto* expression =
      std::get_if<BinaryExpression>(&assignment->expression);
  return expression && expression->op == BinaryOperator::kIn &&
         std::get_if<abc::RegisterId>(&expression->left) &&
         std::get<abc::RegisterId>(expression->left) == key &&
         std::holds_alternative<Accumulator>(expression->right);
}
bool Edge(const abc::MethodBody& body, std::size_t first, Step branch,
          Step target, abc::Opcode opcode) {
  const auto& site = At(body, first, branch);
  const auto& instruction = site.decoded;
  return instruction.opcode == opcode && instruction.operand_count == 1 &&
         instruction.operands[0].kind == abc::OperandKind::kImmediate &&
         site.branch_target &&
         *site.branch_target == At(body, first, target).offset;
}
bool Call(const abc::BodyInstruction& site,
          std::array<abc::RegisterId, 4> registers,
          std::uint64_t frame_size) {
  const auto& instruction = site.decoded;
  if (instruction.opcode != abc::Opcode::kCALLTHIS3_IMM8_V8_V8_V8_V8 ||
      instruction.operand_count != registers.size() + 1 ||
      instruction.operands[0].kind != abc::OperandKind::kImmediate ||
      instruction.operands[0].raw_bits > UINT8_MAX)
    return false;
  for (std::size_t i = 0; i < registers.size(); ++i)
    if (instruction.operands[i + 1].kind != abc::OperandKind::kRegister ||
        instruction.operands[i + 1].raw_bits >= frame_size ||
        instruction.operands[i + 1].raw_bits > UINT8_MAX ||
        instruction.operands[i + 1].raw_bits != registers[i].value)
      return false;
  return true;
}
bool TransientsDead(const abc::MethodBody& body, std::size_t after,
                    std::uint64_t frame_size,
                    std::array<abc::RegisterId, 8> registers) {
  std::array<bool, 8> live{};
  live.fill(true);
  for (std::size_t i = after; i < body.instructions.size(); ++i) {
    const auto& instruction = body.instructions[i].decoded;
    if (instruction.jump || instruction.conditional) return false;
    const auto accessed = abc::GetRegisterAccesses(instruction, frame_size);
    const auto* windows = std::get_if<abc::RegisterAccesses>(&accessed);
    if (!windows) return false;
    for (std::size_t j = 0; j < registers.size(); ++j) {
      if (!live[j]) continue;
      for (std::size_t w = 0; w < windows->count; ++w) {
        const auto& window = windows->windows[w];
        if (registers[j].value < window.first.value ||
            registers[j].value - window.first.value >= window.count)
          continue;
        if (window.access != abc::OperandAccess::kWrite) return false;
      }
    }
    for (std::size_t j = 0; j < registers.size(); ++j) {
      if (!live[j]) continue;
      for (std::size_t w = 0; w < windows->count; ++w) {
        const auto& window = windows->windows[w];
        if (window.access == abc::OperandAccess::kWrite &&
            registers[j].value >= window.first.value &&
            registers[j].value - window.first.value < window.count)
          live[j] = false;
      }
    }
  }
  return true;
}
}  // namespace

std::optional<GuardedGlobalCall> MatchGuardedGlobalCall(
    const abc::MethodBody& body, std::size_t first,
    std::uint64_t frame_size) {
  using abc::Opcode;
  if (!body.tries.empty() || !body.handlers.empty() ||
      first > body.instructions.size() ||
      Offset(Step::kAfter) >= body.instructions.size() - first)
    return {};
  const auto key = String(At(body, first, Step::kKey));
  const auto receiver_global = Global(At(body, first, Step::kReceiverGlobal));
  const auto receiver_property =
      Property(At(body, first, Step::kReceiverProperty));
  const auto call_global = Global(At(body, first, Step::kCallGlobal));
  const auto call_property = Property(At(body, first, Step::kCallProperty));
  if (!key || !receiver_global || !receiver_property || !call_global ||
      !call_property ||
      String(At(body, first, Step::kArgumentKey)) != key ||
      Global(At(body, first, Step::kArgumentGlobal)) != receiver_global ||
      Property(At(body, first, Step::kArgumentProperty)) !=
          receiver_property)
    return {};
  const auto key_reg = Register(At(body, first, Step::kStoreKey),
                                Opcode::kSTA_V8, frame_size);
  const auto receiver_reg = Register(At(body, first, Step::kStoreReceiver),
                                     Opcode::kSTA_V8, frame_size);
  const auto call_reg = Register(At(body, first, Step::kStoreCallGlobal),
                                 Opcode::kSTA_V8, frame_size);
  const auto method_reg = Register(At(body, first, Step::kStoreMethod),
                                   Opcode::kSTA_V8, frame_size);
  const auto argument_global_reg =
      Register(At(body, first, Step::kStoreArgumentGlobal),
               Opcode::kSTA_V8, frame_size);
  const auto object_reg = Register(At(body, first, Step::kStoreObject),
                                   Opcode::kSTA_V8, frame_size);
  const auto argument_key_reg =
      Register(At(body, first, Step::kStoreArgumentKey),
               Opcode::kSTA_V8, frame_size);
  const auto function_reg = Register(At(body, first, Step::kStoreFunction),
                                     Opcode::kSTA_V8, frame_size);
  if (!key_reg || !receiver_reg || !call_reg || !method_reg ||
      !argument_global_reg || !object_reg || !argument_key_reg ||
      !function_reg || *key_reg == *receiver_reg ||
      *call_reg == *method_reg || *call_reg == *argument_global_reg ||
      *call_reg == *object_reg || *call_reg == *argument_key_reg ||
      *call_reg == *function_reg ||
      *method_reg == *argument_global_reg ||
      *method_reg == *object_reg || *method_reg == *argument_key_reg ||
      *method_reg == *function_reg || *object_reg == *argument_key_reg ||
      *object_reg == *function_reg || *argument_key_reg == *function_reg)
    return {};
  if (Register(At(body, first, Step::kLoadReceiver), Opcode::kLDA_V8,
               frame_size) != receiver_reg ||
      !In(At(body, first, Step::kIn), *key_reg, frame_size) ||
      !Unary(At(body, first, Step::kToBoolean),
             UnaryOperator::kToBoolean, frame_size) ||
      !Edge(body, first, Step::kBranchFalse, Step::kFalse,
            Opcode::kJNEZ_IMM8) ||
      !Constant(At(body, first, Step::kTrue), Opcode::kLDTRUE) ||
      !Edge(body, first, Step::kJumpJoin, Step::kInvert,
            Opcode::kJMP_IMM8) ||
      !Constant(At(body, first, Step::kFalse), Opcode::kLDFALSE) ||
      !Unary(At(body, first, Step::kInvert),
             UnaryOperator::kLogicalNot, frame_size) ||
      !Edge(body, first, Step::kSkipCall, Step::kAfter,
            Opcode::kJNEZ_IMM8) ||
      Register(At(body, first, Step::kLoadCallGlobal), Opcode::kLDA_V8,
               frame_size) != call_reg ||
      Register(At(body, first, Step::kLoadArgumentGlobal), Opcode::kLDA_V8,
               frame_size) != argument_global_reg ||
      Register(At(body, first, Step::kLoadMethod), Opcode::kLDA_V8,
               frame_size) != method_reg ||
      !Call(At(body, first, Step::kCall),
            {*call_reg, *object_reg, *argument_key_reg, *function_reg},
            frame_size))
    return {};
  const auto created = LiftFunctionCreation(
      At(body, first, Step::kFunction).decoded);
  const auto* function = std::get_if<FunctionCreation>(&created);
  const auto& join = At(body, first, Step::kAfter).decoded;
  if (!function || function->kind != CreationKind::kFunction ||
      function->home_object != HomeObjectInput::kCurrentFunction ||
      !Global(At(body, first, Step::kArgumentGlobal)) ||
      !join.accumulator_write || join.accumulator_read)
    return {};
  const auto start_pc = At(body, first, Step::kKey).offset;
  const auto after_pc = At(body, first, Step::kAfter).offset;
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto& site = body.instructions[i];
    if (!site.branch_target) continue;
    const bool internal = i == first + Offset(Step::kBranchFalse) ||
                          i == first + Offset(Step::kJumpJoin) ||
                          i == first + Offset(Step::kSkipCall);
    if (!internal && *site.branch_target >= start_pc &&
        *site.branch_target <= after_pc)
      return {};
  }
  if (!TransientsDead(body, first + Offset(Step::kAfter), frame_size,
                      {*key_reg, *receiver_reg, *call_reg, *method_reg,
                       *argument_global_reg, *object_reg, *argument_key_reg,
                       *function_reg}))
    return {};
  return GuardedGlobalCall{
      first, first + Offset(Step::kAfter),
      first + Offset(Step::kFunction), *key, *receiver_global,
      *receiver_property, *call_global, *call_property};
}
}  // namespace abcd::analysis
