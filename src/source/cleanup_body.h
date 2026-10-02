#ifndef ABCD_SOURCE_CLEANUP_BODY_H_
#define ABCD_SOURCE_CLEANUP_BODY_H_

#include "environment_cleanup.h"
#include "abcd/source/cleanup_origin.h"

namespace abcd::source {
// Owns both the original exception metadata and its normal-path view. Every
// decoded instruction, branch target and PC is copied unchanged. Cleanup-only
// sites remain explicit provenance; consumers must not label them globally
// unreachable merely because the normal projection cannot execute them.
class CleanupBody {
 public:
  CleanupBody(CleanupBody&&) noexcept = default;
  CleanupBody& operator=(CleanupBody&&) noexcept = default;
  static std::variant<CleanupBody, CleanupFailure> Build(const abc::MethodBody&,
      abc::ProfileId, const abc::FrameLayout&, analysis::SemanticBudget = {});
  const abc::MethodBody& Original() const { return original_; }
  const abc::MethodBody& Normal() const { return normal_; }
  const EnvironmentCleanup& Proof() const { return proof_; }
  std::optional<CleanupInstructionKind> Kind(analysis::InstructionId id) const {
    return id.value < kinds_.size() ? kinds_[id.value] : std::nullopt;
  }
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  CleanupBody(abc::MethodBody original, abc::MethodBody normal, EnvironmentCleanup proof,
      std::vector<std::optional<CleanupInstructionKind>> kinds, std::size_t bytes, std::uint64_t work)
      : original_(std::move(original)), normal_(std::move(normal)), proof_(std::move(proof)),
        kinds_(std::move(kinds)), bytes_(bytes), work_(work) {}
  abc::MethodBody original_, normal_;
  EnvironmentCleanup proof_;
  std::vector<std::optional<CleanupInstructionKind>> kinds_;
  std::size_t bytes_;
  std::uint64_t work_;
};
}  // namespace abcd::source
#endif
