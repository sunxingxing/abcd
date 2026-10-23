#ifndef ABCD_SOURCE_ENVIRONMENT_CLEANUP_H_
#define ABCD_SOURCE_ENVIRONMENT_CLEANUP_H_

#include "abcd/analysis/exception_environment.h"

namespace abcd::source {
struct CleanupPop {
  analysis::InstructionId instruction;
  analysis::InstructionId allocation;
};
struct CleanupHandler {
  analysis::HandlerId handler;
  analysis::InstructionId rethrow;
  std::uint32_t first_pop;
  std::uint32_t pop_count;
};
enum class CleanupError : std::uint8_t {
  kNoRegions, kHandler, kEntry, kOwnership, kRethrow, kResourceLimit, kWorkLimit
};
using CleanupFailure = std::variant<CleanupError, analysis::SemanticFailure,
    analysis::FlowFailure, analysis::DispatchFailure, analysis::EnvironmentFlowFailure>;

// Proves catch-all, local-environment-only cleanup followed by an unchanged
// pending-exception throw to the caller. Builds original exception topology and
// environment identities itself; no source names or sample identities enter.
// This is NOT permission to erase exception tables: a residual owner must also
// prove normal-path correspondence and retain the cleanup instructions' PCs.
class EnvironmentCleanup {
 public:
  EnvironmentCleanup(EnvironmentCleanup&&) noexcept = default;
  EnvironmentCleanup& operator=(EnvironmentCleanup&&) noexcept = default;
  static std::variant<EnvironmentCleanup, CleanupFailure> Build(
      const abc::MethodBody&, abc::ProfileId, const abc::FrameLayout&,
      analysis::SemanticBudget = {});
  std::span<const CleanupHandler> Handlers() const { return handlers_; }
  std::span<const CleanupPop> Pops() const { return pops_; }
  // Handler acc<->register moves around the pops (optimizer form).
  std::span<const analysis::InstructionId> Moves() const { return moves_; }
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  EnvironmentCleanup() = default;
  std::vector<CleanupHandler> handlers_;
  std::vector<CleanupPop> pops_;
  std::vector<analysis::InstructionId> moves_;
  std::size_t bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
