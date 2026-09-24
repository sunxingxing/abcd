#ifndef ABCD_ANALYSIS_EXCEPTION_SSA_H_
#define ABCD_ANALYSIS_EXCEPTION_SSA_H_
#include <utility>
#include "abcd/analysis/exception_flow.h"

namespace abcd::analysis {
class ExceptionSsa {
 public:
  ExceptionSsa(ExceptionSsa&&) noexcept = default;
  ExceptionSsa& operator=(ExceptionSsa&&) noexcept = default;
  // Requires the unchanged output of BuildExceptionFlow. Site/phi indices in
  // Values/Inputs refer to flow.nodes, never directly to ABC instructions.
  // Catch definitions are not yet a Language-value or environment-state proof.
  [[nodiscard]] static std::variant<ExceptionSsa, RegisterUseFailure, IdfError,
                                    DominatorError, SsaError>
  Build(const ExceptionFlow& flow, std::uint64_t frame_size,
        IdfQuota& quota, IdfBudget budget = {});
  [[nodiscard]] std::span<const SsaRead> Reads(ExceptionNodeId node) const {
    return registers_.Reads(InstructionId{node.value});
  }
  [[nodiscard]] std::optional<SsaValueId> Write(ExceptionNodeId node) const {
    return registers_.Write(InstructionId{node.value});
  }
  [[nodiscard]] std::span<const RegisterValue> Values() const { return registers_.Values(); }
  [[nodiscard]] std::span<const PhiInput> Inputs(SsaValueId phi) const {
    return registers_.Inputs(phi);
  }
  [[nodiscard]] const SsaWork& Work() const { return registers_.work; }
  [[nodiscard]] std::size_t AllocatedBytes() const { return registers_.AllocatedBytes(); }
 private:
  explicit ExceptionSsa(RegisterSsa registers) : registers_(std::move(registers)) {}
  RegisterSsa registers_;
};
}  // namespace abcd::analysis
#endif
