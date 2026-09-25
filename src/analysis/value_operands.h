#ifndef ABCD_ANALYSIS_VALUE_OPERANDS_H_
#define ABCD_ANALYSIS_VALUE_OPERANDS_H_

#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
enum class OperandRole : std::uint8_t {
  kInput, kBase, kKey, kStoredValue, kLeft, kRight, kCondition,
  kReturnValue, kGuardValue, kDiagnosticName
};
enum class ReadPurpose : std::uint8_t { kOperand, kProtocol };
struct InputOperand {
  OperandRole role;
  ScalarValue value;
  ReadPurpose purpose = ReadPurpose::kOperand;
  std::optional<std::uint16_t> read_ordinal;
};
// Bounded G1 adapter. Calls/ranges have a separate variable-sized adapter;
// they must never be truncated to this fixed scalar capacity.
inline constexpr std::size_t kScalarOperandCapacity = kFixedRegisterReads;
struct ValueOperands {
  std::array<InputOperand, kScalarOperandCapacity> operands{};
  std::uint8_t count = 0;
  std::optional<ScalarSlot> write;
};
enum class OperandError : std::uint8_t {
  kUnsupportedOperation, kReadCount, kReadSlot, kWriteSlot
};
// Derive roles from typed fields first, then check their occurrences against
// the independent register-effect contract. Equal slots remain distinct uses.
[[nodiscard]] std::variant<ValueOperands, OperandError> DescribeValueOperands(
    const SemanticInstruction& instruction);
}  // namespace abcd::analysis
#endif
