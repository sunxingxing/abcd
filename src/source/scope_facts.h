#ifndef ABCD_SOURCE_SCOPE_FACTS_H_
#define ABCD_SOURCE_SCOPE_FACTS_H_

#include "abcd/analysis/semantic_validation.h"

namespace abcd::source {
using CaptureCell = std::variant<analysis::LocalCell, analysis::CapturedSlot>;
enum class ScopeError : std::uint8_t {
  kSite, kEnvironment, kCell, kInitialization, kWrites, kDomain
};
// Borrowed view of ONE freshly verified, unchanged semantic owner. The verifier
// owns sorted cell/store/barrier/guard indexes; no names or source text certify
// these facts. Keep all inputs alive and unchanged. Allocation IDs identify
// sites, not dynamic cell instances, especially on loop re-entry.
class ScopeFacts {
 public:
  ScopeFacts(const analysis::SemanticMethod& method, const analysis::SemanticProof& proof)
      : ScopeFacts(method, proof.environments, proof.locals, proof.defined_captures,
                   proof.closure_writes) {}
  ScopeFacts(const analysis::SemanticMethod& method,
      const analysis::EnvironmentFlow& environments, const analysis::LocalAccessPlan& locals,
      const analysis::DefinedCaptures& defined, analysis::ClosureWrites writes)
      : method_(method), environments_(environments), locals_(locals), defined_(defined), writes_(writes) {}
  [[nodiscard]] std::variant<CaptureCell, ScopeError> Rebase(
      analysis::InstructionId creation, analysis::CapturedSlot capture,
      analysis::CaptureDomain requirement) const;
  [[nodiscard]] bool Matches(analysis::InstructionId site,
      const analysis::CreationBinding& creation) const;
  // Conservative logarithmic comparison/query bound; batch users debit this
  // before each query. Environment ancestry has a fixed uint32 depth domain.
  [[nodiscard]] std::uint64_t QueryWork() const;

 private:
  const analysis::SemanticMethod& method_;
  const analysis::EnvironmentFlow& environments_;
  const analysis::LocalAccessPlan& locals_;
  const analysis::DefinedCaptures& defined_;
  analysis::ClosureWrites writes_;
};
}  // namespace abcd::source
#endif
