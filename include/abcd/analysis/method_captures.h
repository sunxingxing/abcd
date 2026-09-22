#ifndef ABCD_ANALYSIS_METHOD_CAPTURES_H_
#define ABCD_ANALYSIS_METHOD_CAPTURES_H_

#include "abcd/analysis/capture_summary.h"

namespace abcd::analysis {
struct CaptureOrigin {
  corpus::Sha256 file;
  abc::ProfileId profile;
  abc::MethodPosition method;
};
enum class MethodCaptureError {
  kContextMismatch,
  kMethodRange,
  kCreationMissing,
  kTargetMismatch,
  kCaptureMissing,
  kResourceLimit
};
using MethodCaptureFailure =
    std::variant<MethodCaptureError, CaptureSummaryError, CreationError,
                 CreationBindingError, abc::MethodTargetError,
                 abc::MetadataError, abc::MethodAttributeError>;
struct MethodCaptureBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// Owned, immutable modern-creation capture facts for one method. Build consumes
// only flow.environments on success; the remaining flow must not be reused for
// analysis after transfer. It never loads a target body. Caller must
// supply the genuine once-per-file fingerprint and unchanged matching inputs.
// Payload budget includes transferred ancestry + build scratch + retained
// summaries/sites. Body/graph/other flow vectors and object overhead are caller
// admission. This synchronous stage is not an asynchronous cache service.
class MethodCaptures {
 public:
  MethodCaptures(MethodCaptures&&) noexcept = default;
  MethodCaptures& operator=(MethodCaptures&&) noexcept = default;
  [[nodiscard]] static std::variant<MethodCaptures, MethodCaptureFailure> Build(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      CaptureOrigin origin, const abc::MethodBody& body, EnvironmentFlow&& flow,
      MethodCaptureBudget budget = {});
  // Resolve a direct captured use recorded by target.Captures(). A requirement
  // used only by a nested closure needs later demand propagation; absence here
  // is not proof of invalid bytecode. No graph traversal or target decoding.
  [[nodiscard]] std::variant<LexicalBinding, MethodCaptureError,
                             EnvironmentError, CaptureRebaseError>
  Resolve(abc::CodeOffset creation, const MethodCaptures& target,
          CapturedSlot captured) const;
  [[nodiscard]] const CaptureOrigin& Origin() const { return origin_; }
  [[nodiscard]] const CaptureSummary& Captures() const { return captures_; }
  [[nodiscard]] std::span<const CreationBinding> Creations() const {
    return {creations_.get(), creations_ ? size_ : 0};
  }
  [[nodiscard]] std::size_t AllocatedBytes() const;
  [[nodiscard]] std::size_t PeakBytes() const {
    return AllocatedBytes() ? peak_ : 0;
  }

 private:
  MethodCaptures(CaptureOrigin origin, EnvironmentIndex&& environments,
                 CaptureSummary&& captures,
                 std::unique_ptr<CreationBinding[]>&& creations,
                 std::size_t size, std::size_t peak);
  CaptureOrigin origin_;
  EnvironmentIndex environments_;
  CaptureSummary captures_;
  std::unique_ptr<CreationBinding[]> creations_;
  std::size_t size_ = 0;
  std::size_t peak_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_METHOD_CAPTURES_H_
