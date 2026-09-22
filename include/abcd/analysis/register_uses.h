#ifndef ABCD_ANALYSIS_REGISTER_USES_H_
#define ABCD_ANALYSIS_REGISTER_USES_H_

#include "abcd/analysis/phi_placement.h"
#include "abcd/analysis/register_effect.h"

namespace abcd::analysis {
struct RegisterBindingId {
  std::size_t value = 0;
};
struct RegisterBinding {
  ScalarSlot slot;
  std::size_t definitions_begin = 0;
  std::size_t definitions_count = 0;
  std::size_t upward_begin = 0;
  std::size_t upward_count = 0;
};
enum class RegisterUseError {
  kExceptionRegions,
  kInvalidGraph,
  kResumeEdge,
  kInstruction,
  kWorkLimit,
  kResourceLimit
};
struct RegisterUseFailure {
  RegisterUseError kind;
  abc::CodeOffset offset{};
  std::optional<ScalarError> instruction;
};
struct RegisterUseWork {
  std::uint64_t instructions = 0;
  std::uint64_t accesses = 0;
  std::uint64_t units = 0;
  std::size_t peak_bytes = 0;
};

// Owned sparse register/acc def and upward-use sets for normal entry block 0.
// Requires validated body and its UNCHANGED BuildNormalFlow result. Structural
// checks do not establish body/graph identity. Reject handlers/resumes and any
// reachable instruction lacking proven register effects. No lexical-cell
// promotion, value proof, SSA renaming or function task service is implied.
class RegisterUses {
 public:
  RegisterUses(RegisterUses&&) noexcept = default;
  RegisterUses& operator=(RegisterUses&&) noexcept = default;
  [[nodiscard]] static std::variant<RegisterUses, RegisterUseFailure> Build(
      const abc::MethodBody& body, const NormalFlow& graph,
      std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget = {});
  [[nodiscard]] std::span<const RegisterBinding> Bindings() const {
    return {bindings_.get(), bindings_ ? count_ : 0};
  }
  [[nodiscard]] std::optional<RegisterBindingId> Find(ScalarSlot slot) const;
  // Invalid IDs (including moved-from results) yield empty spans.
  [[nodiscard]] std::span<const BlockId> Definitions(
      RegisterBindingId id) const;
  [[nodiscard]] std::span<const BlockId> UpwardUses(RegisterBindingId id) const;
  [[nodiscard]] std::size_t AllocatedBytes() const;
  RegisterUseWork work;

 private:
  RegisterUses() = default;
  std::unique_ptr<RegisterBinding[]> bindings_;
  std::unique_ptr<BlockId[]> definitions_;
  std::unique_ptr<BlockId[]> upward_;
  std::size_t count_ = 0;
  std::size_t definitions_count_ = 0;
  std::size_t upward_count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REGISTER_USES_H_
