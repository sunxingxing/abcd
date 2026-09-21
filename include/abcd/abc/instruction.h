#ifndef ABCD_ABC_INSTRUCTION_H_
#define ABCD_ABC_INSTRUCTION_H_

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

#include "abcd/abc/generated_isa.h"
#include "abcd/abc/header.h"

namespace abcd::abc {

enum class OperandKind : std::uint8_t {
  kRegister,
  kImmediate,
  kMethod,
  kType,
  kField,
  kString,
  kLiteralArray,
};

enum class OperandAccess : std::uint8_t {
  kRead,
  kWrite,
  kReadWrite,
};

struct InstructionOperand {
  OperandKind kind = OperandKind::kImmediate;
  OperandAccess access = OperandAccess::kRead;
  std::uint64_t raw_bits = 0;
  std::uint8_t width_bits = 0;
  bool is_signed = false;
  bool is_float = false;
};

struct DecodedInstruction {
  Opcode opcode = Opcode::kLDUNDEFINED;
  std::uint8_t size = 0;
  std::uint8_t operand_count = 0;
  std::array<InstructionOperand, kMaxInstructionOperands> operands{};
  std::optional<std::int64_t> branch_delta;
  bool jump = false;
  bool conditional = false;
  bool accumulator_read = false;
  bool accumulator_write = false;
};

enum class InstructionError {
  kUnsupportedProfile,
  kTruncated,
  kInvalidOpcode,
  kInvalidEncoding,
};

using InstructionResult = std::variant<DecodedInstruction, InstructionError>;

// Decodes one instruction. The caller retains input bytes and checks method
// boundaries, branch targets and operand references separately.
[[nodiscard]] InstructionResult DecodeInstruction(
    std::span<const std::uint8_t> remaining, ProfileId profile);

}  // namespace abcd::abc

#endif  // ABCD_ABC_INSTRUCTION_H_
