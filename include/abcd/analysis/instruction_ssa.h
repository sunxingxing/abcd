#ifndef ABCD_ANALYSIS_INSTRUCTION_SSA_H_
#define ABCD_ANALYSIS_INSTRUCTION_SSA_H_

#include "abcd/analysis/exception_ssa.h"

namespace abcd::analysis {
enum class InstructionSsaError : std::uint8_t { kInvalidFlow, kResourceLimit };

// Instruction-coordinate facade over immutable normal or phased SSA. Owners
// must outlive this view. No values, phi inputs or bytecode are reconstructed.
// Borrowed SsaRead/PhiInput records retain their native site/edge coordinates;
// consumers use their value IDs, not those coordinates as instruction indices.
class InstructionSsa {
 public:
  explicit InstructionSsa(const RegisterSsa& normal) : ssa_(&normal) {}
  InstructionSsa(InstructionSsa&&) noexcept = default;
  InstructionSsa& operator=(InstructionSsa&&) noexcept = default;
  // Matching unchanged flow/SSA. O(I+N) construction and O(I) owned index;
  // queries borrow the original arrays. No opcode interpretation or SSA pass.
  [[nodiscard]] static std::variant<InstructionSsa, InstructionSsaError> Build(
      const ExceptionFlow& flow, const ExceptionSsa& ssa,
      std::size_t bytes = IdfBudget{}.bytes);
  [[nodiscard]] std::span<const SsaRead> Reads(InstructionId instruction) const;
  [[nodiscard]] std::optional<SsaValueId> Write(InstructionId instruction) const;
  [[nodiscard]] std::span<const PhiInput> Inputs(SsaValueId value) const;
  [[nodiscard]] std::optional<ScalarSlot> Slot(SsaValueId value) const;
  [[nodiscard]] bool IsPhi(SsaValueId value) const;
  // Only ordinary/normal Commit definitions map to bytecode instructions.
  // Catch is a distinct VM definition, never the handler's first instruction.
  [[nodiscard]] std::optional<InstructionId> Definition(SsaValueId value) const;
  [[nodiscard]] std::size_t ValueCount() const;
  [[nodiscard]] std::size_t AllocatedBytes() const;
  [[nodiscard]] bool IsPhased() const { return flow_ != nullptr; }

 private:
  struct Sites {
    std::optional<ExceptionNodeId> before;
    std::optional<ExceptionNodeId> commit;
  };
  InstructionSsa(const ExceptionFlow& flow, const ExceptionSsa& ssa)
      : ssa_(&ssa), flow_(&flow) {}
  [[nodiscard]] std::span<const RegisterValue> Values() const;
  std::variant<const RegisterSsa*, const ExceptionSsa*> ssa_;
  const ExceptionFlow* flow_ = nullptr;
  std::vector<Sites> sites_;
};
}  // namespace abcd::analysis
#endif
