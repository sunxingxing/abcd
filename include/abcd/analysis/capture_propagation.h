#ifndef ABCD_ANALYSIS_CAPTURE_PROPAGATION_H_
#define ABCD_ANALYSIS_CAPTURE_PROPAGATION_H_

#include "abcd/analysis/method_captures.h"

namespace abcd::analysis {
struct CaptureContextId {
  std::uint32_t value = 0;
  auto operator<=>(const CaptureContextId&) const = default;
};
struct CaptureFactId {
  std::uint32_t value = 0;
  auto operator<=>(const CaptureFactId&) const = default;
};
struct CaptureParent {
  CaptureContextId context;
  abc::CodeOffset creation;
};
struct CaptureContext {
  const MethodCaptures* method = nullptr;
  std::optional<CaptureParent> parent;
};
struct CaptureContextInfo {
  CaptureOrigin origin;
  std::optional<CaptureParent> parent;
};
struct CaptureRequirement {
  CaptureContextId context;
  CapturedSlot captured;
};
struct CaptureLocal {
  CaptureContextId context;
  EnvironmentId environment;
  LexicalSlot slot;
};
struct CaptureBoundary {};
struct CaptureFact {
  CaptureRequirement requirement;
  // A fact reference can point forward numerically. Context ancestry, not
  // insertion order, makes this DAG acyclic. Boundary retains the requirement.
  std::variant<CaptureBoundary, CaptureLocal, CaptureFactId> destination;
};
enum class CapturePropagationError {
  kInvalidContext,
  kInvalidParent,
  kMissingSeed,
  kResourceLimit
};
struct CapturePropagationFailure {
  std::variant<CapturePropagationError, MethodCaptureError, EnvironmentError,
               CaptureRebaseError>
      cause;
  CaptureContextId context;
  std::optional<CapturedSlot> captured;
};
struct CapturePropagationBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// Demand-driven transfer over a caller-supplied finite context forest. Parents
// precede children; repeated method snapshots remain distinct contexts. Seeds
// require direct-use evidence. Only demanded creation edges are checked.
// This does not discover all creation contexts, unroll recursion, establish
// runtime cell identity, or prove values/TDZ. Inputs are borrowed during Build;
// successful results own their context registry and shared fact DAG.
class CapturePropagation {
 public:
  CapturePropagation(CapturePropagation&&) noexcept = default;
  CapturePropagation& operator=(CapturePropagation&&) noexcept = default;
  [[nodiscard]] static std::variant<CapturePropagation,
                                    CapturePropagationFailure>
  Build(std::span<const CaptureContext> contexts,
        std::span<const CaptureRequirement> seeds,
        CapturePropagationBudget budget = {});
  [[nodiscard]] std::span<const CaptureContextInfo> Contexts() const {
    return {contexts_.get(), contexts_ ? context_count_ : 0};
  }
  [[nodiscard]] std::span<const CaptureFact> Facts() const {
    return {facts_.get(), facts_ ? fact_count_ : 0};
  }
  [[nodiscard]] const CaptureFact* Find(CaptureRequirement requirement) const;
  [[nodiscard]] const CaptureFact* Find(CaptureFactId id) const;
  [[nodiscard]] std::uint64_t Transfers() const {
    return facts_ ? transfers_ : 0;
  }
  [[nodiscard]] std::size_t AllocatedBytes() const;
  // Includes PMR map nodes, vector-growth overlap and final arrays while
  // scratch is live. Caller inputs, allocator overhead and stack are excluded.
  [[nodiscard]] std::size_t PeakBytes() const { return contexts_ ? peak_ : 0; }

 private:
  CapturePropagation() = default;
  std::unique_ptr<CaptureContextInfo[]> contexts_;
  std::unique_ptr<CaptureFact[]> facts_;
  std::unique_ptr<CaptureFactId[]> index_;
  std::size_t context_count_ = 0;
  std::size_t fact_count_ = 0;
  std::size_t peak_ = 0;
  std::uint64_t transfers_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_CAPTURE_PROPAGATION_H_
