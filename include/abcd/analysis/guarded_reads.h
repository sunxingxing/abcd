#ifndef ABCD_ANALYSIS_GUARDED_READS_H_
#define ABCD_ANALYSIS_GUARDED_READS_H_

#include "abcd/analysis/register_value_flow.h"

namespace abcd::analysis {
struct GuardedRead {
  // Inherits SSA's may-read contract: conditional diagnostic-name reads are
  // not made unconditional. Evidence concerns a value, not effect scheduling.
  SsaRead read;
  ValueKinds kinds;
  std::optional<InstructionId> guard;
};
enum class GuardReadError {
  kExceptionRegions,
  kResumeEdge,
  kInvalidGraph,
  kInvalidSsa,
  kInstruction,
  kWorkLimit,
  kResourceLimit
};
struct GuardReadWork {
  std::uint64_t instructions = 0;
  std::uint64_t guards = 0;
  std::uint64_t restores = 0;
  std::uint64_t units = 0;
  std::size_t peak_bytes = 0;
};

// Requires the same unchanged validated body/normal CFG/SSA/value flow/frame.
// Only same-value, dominating, normally completed Hole guards refine reads.
// Non-Hole is NOT necessarily a language value. No effect elimination, copy
// equivalence, exceptional/heap/ABI proof or source equivalence is implied.
class GuardedReads {
 public:
  GuardedReads(GuardedReads&&) noexcept = default;
  GuardedReads& operator=(GuardedReads&&) noexcept = default;
  [[nodiscard]] static std::variant<GuardedReads, GuardReadError> Build(
      const abc::MethodBody& body, const NormalFlow& graph,
      const RegisterSsa& ssa, const RegisterValueFlow& flow,
      std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget = {});
  [[nodiscard]] std::span<const GuardedRead> Reads() const {
    return {reads_.get(), reads_ ? count_ : 0};
  }
  [[nodiscard]] std::optional<GuardedRead> At(InstructionId instruction,
                                              std::uint32_t ordinal) const;
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return Reads().size_bytes();
  }
  GuardReadWork work;

 private:
  GuardedReads() = default;
  std::unique_ptr<GuardedRead[]> reads_;
  std::size_t count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_GUARDED_READS_H_
