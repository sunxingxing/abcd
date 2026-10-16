#ifndef ABCD_SOURCE_PROTOCOL_REGION_OWNERSHIP_H_
#define ABCD_SOURCE_PROTOCOL_REGION_OWNERSHIP_H_
#include "close_completion.h"
namespace abcd::source {
// A summary is diagnostic data. Only independent original-flow verification
// can mint ownership; one missing close prevents projection of the whole method.
class VerifiedProtocolRegionOwnership {
 public:
  VerifiedProtocolRegionOwnership(VerifiedProtocolRegionOwnership&&) noexcept = default;
  VerifiedProtocolRegionOwnership& operator=(VerifiedProtocolRegionOwnership&&) noexcept = default;
  const CloseCompletionSummary& Summary() const { return summary_; }
  const IteratorKeys& Keys() const { return keys_; }
  const IteratorProtocolScan& Scan() const { return scan_; }
  abc::ProfileId Profile() const { return profile_; }
  SourceContract Contract() const { return contract_; }
  std::uint64_t VerificationWork() const { return verification_work_; }
 private:
  VerifiedProtocolRegionOwnership(CloseCompletionSummary summary, IteratorKeys keys,
      IteratorProtocolScan scan, abc::ProfileId profile, SourceContract contract, std::uint64_t work)
      : summary_(std::move(summary)), keys_(std::move(keys)), scan_(std::move(scan)), profile_(profile), contract_(contract), verification_work_(work) {}
  CloseCompletionSummary summary_;
  IteratorKeys keys_;
  IteratorProtocolScan scan_;
  abc::ProfileId profile_;
  SourceContract contract_;
  std::uint64_t verification_work_;
  friend std::variant<VerifiedProtocolRegionOwnership, CloseCompletionFailure> VerifyProtocolRegionOwnership(
      const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
      const analysis::PhasedValueInput&, const ExceptionPlan&, IteratorKeys,
      IteratorProtocolScan, CloseCompletionSummary, analysis::SemanticBudget, SourceContract);
};
[[nodiscard]] std::variant<VerifiedProtocolRegionOwnership, CloseCompletionFailure> VerifyProtocolRegionOwnership(
    const abc::MethodBody&, abc::ProfileId, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, IteratorKeys,
    IteratorProtocolScan, CloseCompletionSummary, analysis::SemanticBudget = {}, SourceContract = kSourceContract);
}
#endif
