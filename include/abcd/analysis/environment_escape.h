#ifndef ABCD_ANALYSIS_ENVIRONMENT_ESCAPE_H_
#define ABCD_ANALYSIS_ENVIRONMENT_ESCAPE_H_

#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/register_ssa.h"

namespace abcd::analysis {
enum class EscapeError {
  kInvalidInput,
  kInstruction,
  kOpaqueValue,
  kWorkLimit,
  kResourceLimit
};
struct EscapeWork {
  std::uint64_t unions = 0;
  std::uint64_t find_steps = 0;
  std::uint64_t edge_visits = 0;
  std::uint64_t units = 0;
  std::size_t cells = 0;
  std::size_t peak_bytes = 0;
};
// Same unchanged validated body/frame, SSA and ordinary environment flow.
// Conservative allocation-site escape, NOT cell initialization or permission
// to merge dynamic instances/erase allocations. No borrowed result storage.
// Calls/creations and non-lexical uses conservatively expose their inputs.
// Raw NaNs/unmodeled numeric representation transformations fail closed, not
// a local-identity proof for forged tagged references or arbitrary native code.
// Bounded synchronous primitive; inputs and function-task scheduling are caller
// responsibilities. Cell initialization/value/TDZ proof remains separate.
class EnvironmentEscape {
 public:
  EnvironmentEscape(EnvironmentEscape&&) noexcept = default;
  EnvironmentEscape& operator=(EnvironmentEscape&&) noexcept = default;
  [[nodiscard]] static std::variant<EnvironmentEscape, EscapeError> Build(
      const abc::MethodBody& body, const RegisterSsa& ssa,
      const EnvironmentFlow& environments, std::uint64_t frame_size,
      IdfQuota& quota, IdfBudget budget = {});
  [[nodiscard]] std::optional<bool> MayEscape(EnvironmentId id) const {
    if (!escaped_ || id.value >= count_) return {};
    return escaped_[id.value] != 0;
  }
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return escaped_ ? count_ * sizeof(std::uint8_t) : 0;
  }
  EscapeWork work;

 private:
  struct Builder;
  EnvironmentEscape() = default;
  std::unique_ptr<std::uint8_t[]> escaped_;
  std::size_t count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ENVIRONMENT_ESCAPE_H_
