#include "abcd/analysis/first_for_in.h"

#include <array>
#include <bit>
#include <climits>
#include <cstdint>

#include "abcd/analysis/iterator.h"

namespace abcd::analysis {
namespace {
using abc::BodyInstruction;
using abc::Opcode;

enum Site : std::size_t {
  kSource,
  kPropertyIterator,
  kStoreIterator,
  kSkipHandler,
  kStoreException,
  kReloadException,
  kRethrow,
  kNextName,
  kStoreName,
  kLoadUndefined,
  kCompareUndefined,
  kExitWhenUndefined,
  kReloadName,
  kReturnName,
  kReturnUndefined,
  kSiteCount
};

constexpr std::array<Opcode, kSiteCount> kPattern{
    Opcode::kLDA_V8,          Opcode::kGETPROPITERATOR,
    Opcode::kSTA_V8,          Opcode::kJMP_IMM8,
    Opcode::kSTA_V8,          Opcode::kLDA_V8,
    Opcode::kTHROW_PREF_NONE, Opcode::kGETNEXTPROPNAME_V8,
    Opcode::kSTA_V8,          Opcode::kLDUNDEFINED,
    Opcode::kEQ_IMM8_V8,      Opcode::kJNEZ_IMM8,
    Opcode::kLDA_V8,          Opcode::kRETURN,
    Opcode::kRETURNUNDEFINED};

bool Empty(const BodyInstruction& site) {
  return site.decoded.operand_count == 0 && !site.branch_target &&
         !site.decoded.branch_delta;
}

std::optional<abc::RegisterId> Register(const BodyInstruction& site,
                                        std::uint32_t frame_size) {
  if (site.decoded.operand_count != 1 || site.branch_target ||
      site.decoded.branch_delta ||
      site.decoded.operands[0].kind != abc::OperandKind::kRegister ||
      site.decoded.operands[0].raw_bits > UINT8_MAX ||
      site.decoded.operands[0].raw_bits >= frame_size)
    return {};
  return abc::RegisterId{
      static_cast<std::uint32_t>(site.decoded.operands[0].raw_bits)};
}

bool Branch(const BodyInstruction& site, abc::CodeOffset target) {
  if (site.decoded.operand_count != 1 || !site.branch_target ||
      *site.branch_target != target || !site.decoded.branch_delta ||
      site.decoded.operands[0].kind != abc::OperandKind::kImmediate ||
      site.decoded.operands[0].raw_bits > UINT8_MAX ||
      std::bit_cast<std::int8_t>(
          static_cast<std::uint8_t>(site.decoded.operands[0].raw_bits)) !=
          *site.decoded.branch_delta)
    return false;
  return std::int64_t{site.offset.value} + *site.decoded.branch_delta ==
         target.value;
}

bool NameComparison(const BodyInstruction& site, abc::RegisterId name) {
  return site.decoded.operand_count == 2 && !site.branch_target &&
         !site.decoded.branch_delta &&
         site.decoded.operands[0].kind == abc::OperandKind::kImmediate &&
         site.decoded.operands[0].raw_bits <= UINT8_MAX &&
         site.decoded.operands[1].kind == abc::OperandKind::kRegister &&
         site.decoded.operands[1].raw_bits == name.value;
}
}  // namespace

std::optional<FirstForIn> MatchFirstForIn(const abc::MethodBody& body,
                                         const abc::FrameLayout& frame) {
  // v7 ForInStatement::Compile emits a transparent catch around enumeration
  // setup. Keep the matcher whole-body and constant-size; broader CFGs need
  // exception-aware data flow before they can be published as source.
  const auto& instructions = body.instructions;
  const bool leading_nop = !instructions.empty() &&
                           instructions.front().decoded.opcode == Opcode::kNOP;
  const std::size_t start = leading_nop ? 1 : 0;
  if (instructions.size() != start + kSiteCount || body.tries.size() != 1 ||
      body.handlers.size() != 1 || frame.registers == 0 ||
      frame.first_parameter.value > frame.registers ||
      frame.parameters > frame.registers - frame.first_parameter.value)
    return {};
  if (instructions.front().offset.value != 0) return {};
  if (leading_nop && !Empty(instructions.front())) return {};
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    if (instructions[i].decoded.size == 0 ||
        (i != 0 && std::uint64_t{instructions[i - 1].offset.value} +
                           instructions[i - 1].decoded.size !=
                       instructions[i].offset.value))
      return {};
  }
  const auto at = [&](Site site) -> const BodyInstruction& {
    return instructions[start + site];
  };
  for (std::size_t i = 0; i < kSiteCount; ++i)
    if (instructions[start + i].decoded.opcode != kPattern[i]) return {};
  const auto source = Register(at(kSource), frame.registers);
  const auto iterator = Register(at(kStoreIterator), frame.registers);
  const auto exception = Register(at(kStoreException), frame.registers);
  const auto reloaded_exception = Register(at(kReloadException), frame.registers);
  const auto name = Register(at(kStoreName), frame.registers);
  const auto reloaded_name = Register(at(kReloadName), frame.registers);
  if (!source || !iterator || !exception || !reloaded_exception || !name ||
      !reloaded_name || *exception != *reloaded_exception ||
      *name != *reloaded_name ||
      source->value < frame.first_parameter.value ||
      source->value - frame.first_parameter.value >= frame.parameters ||
      *iterator == *source || *exception == *source || *name == *source ||
      *iterator == *exception || *iterator == *name)
    return {};
  // A reused exception/name register is valid: their lifetimes do not overlap.
  if (!Empty(at(kPropertyIterator)) || !Empty(at(kRethrow)) ||
      !Empty(at(kLoadUndefined)) || !Empty(at(kReturnName)) ||
      !Empty(at(kReturnUndefined)) ||
      !Branch(at(kSkipHandler), at(kNextName).offset) ||
      !Branch(at(kExitWhenUndefined), at(kReturnUndefined).offset) ||
      !NameComparison(at(kCompareUndefined), *name))
    return {};
  const auto next = LiftIteratorInstruction(at(kNextName).decoded,
                                            frame.registers);
  const auto* property = std::get_if<NextPropertyName>(&next);
  if (!property || property->iterator != *iterator) return {};
  const auto& region = body.tries.front();
  const auto& handler = body.handlers.front();
  if (region.offset != at(kSource).offset ||
      std::uint64_t{region.offset.value} + region.length !=
          at(kSkipHandler).offset.value ||
      region.first_handler != 0 || region.handler_count != 1 ||
      handler.type || handler.offset != at(kStoreException).offset ||
      handler.length != 0)
    return {};
  return FirstForIn{*source};
}
}  // namespace abcd::analysis
