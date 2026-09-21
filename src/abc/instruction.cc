#include "abcd/abc/instruction.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace abcd::abc {
namespace {

constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kPrefixWidth = 2;
constexpr std::size_t kMaxOperandBits = 64;

struct OperandSpec {
  OperandKind kind;
  OperandAccess access;
  std::uint8_t width_bits;
  std::uint16_t bit_offset;
  bool is_signed;
  bool is_float;
};

struct InstructionSpec {
  Opcode opcode;
  std::uint8_t size;
  std::uint8_t operand_count;
  std::array<OperandSpec, kMaxInstructionOperands> operands;
  bool jump;
  bool conditional;
  bool accumulator_read;
  bool accumulator_write;
};

// Generated from pinned runtime_core isa.yaml through isapi.rb. Never infer
// semantics by spelling or printed bytecode in this decoder.
#include "abcd/abc/generated_isa_table.inc"

bool IsSupported(ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      return true;
    case ProfileId::kUnknown:
    case ProfileId::kStatic0107:
      return false;
  }
  return false;
}

bool IsPrefix(std::uint8_t byte) {
  return std::find(kPrefixBytes.begin(), kPrefixBytes.end(), byte) !=
         kPrefixBytes.end();
}

const InstructionSpec* FindSpec(std::uint16_t opcode) {
  const auto it = std::lower_bound(
      kInstructionSpecs.begin(), kInstructionSpecs.end(), opcode,
      [](const InstructionSpec& spec, std::uint16_t value) {
        return static_cast<std::uint16_t>(spec.opcode) < value;
      });
  return it == kInstructionSpecs.end() ||
                 static_cast<std::uint16_t>(it->opcode) != opcode
             ? nullptr
             : &*it;
}

std::uint64_t ExtractBits(std::span<const std::uint8_t> bytes,
                          std::uint16_t offset, std::uint8_t width) {
  std::uint64_t value = 0;
  for (std::uint8_t bit = 0; bit < width; ++bit) {
    const std::size_t absolute = static_cast<std::size_t>(offset) + bit;
    const std::uint64_t part =
        (bytes[absolute / kBitsPerByte] >> (absolute % kBitsPerByte)) & 1U;
    value |= part << bit;
  }
  return value;
}

std::int64_t SignExtend(std::uint64_t value, std::uint8_t width) {
  if (width < kMaxOperandBits && ((value >> (width - 1U)) & 1U) != 0) {
    value |= UINT64_MAX << width;
  }
  return std::bit_cast<std::int64_t>(value);
}

}  // namespace

InstructionResult DecodeInstruction(std::span<const std::uint8_t> remaining,
                                    ProfileId profile) {
  if (!IsSupported(profile)) return InstructionError::kUnsupportedProfile;
  if (remaining.empty()) return InstructionError::kTruncated;
  const std::uint8_t primary = remaining.front();
  const bool prefixed = IsPrefix(primary);
  if (prefixed && remaining.size() < kPrefixWidth)
    return InstructionError::kTruncated;
  const std::uint16_t opcode =
      prefixed
          ? (static_cast<std::uint16_t>(remaining[1]) << kBitsPerByte) | primary
          : primary;
  const InstructionSpec* spec = FindSpec(opcode);
  if (!spec) return InstructionError::kInvalidOpcode;
  if (remaining.size() < spec->size) return InstructionError::kTruncated;
  if (spec->operand_count > kMaxInstructionOperands)
    return InstructionError::kInvalidEncoding;

  DecodedInstruction decoded;
  decoded.opcode = spec->opcode;
  decoded.size = spec->size;
  decoded.operand_count = spec->operand_count;
  decoded.jump = spec->jump;
  decoded.conditional = spec->conditional;
  decoded.accumulator_read = spec->accumulator_read;
  decoded.accumulator_write = spec->accumulator_write;
  for (std::size_t index = 0; index < spec->operand_count; ++index) {
    const OperandSpec& source = spec->operands[index];
    if (source.width_bits == 0 || source.width_bits > kMaxOperandBits ||
        static_cast<std::size_t>(source.bit_offset) + source.width_bits >
            static_cast<std::size_t>(spec->size) * kBitsPerByte)
      return InstructionError::kInvalidEncoding;
    InstructionOperand& operand = decoded.operands[index];
    operand.kind = source.kind;
    operand.access = source.access;
    operand.width_bits = source.width_bits;
    operand.is_signed = source.is_signed;
    operand.is_float = source.is_float;
    operand.raw_bits =
        ExtractBits(remaining, source.bit_offset, source.width_bits);
    if (spec->jump && source.kind == OperandKind::kImmediate &&
        source.is_signed && !decoded.branch_delta) {
      decoded.branch_delta = SignExtend(operand.raw_bits, source.width_bits);
    }
  }
  return decoded;
}

}  // namespace abcd::abc
