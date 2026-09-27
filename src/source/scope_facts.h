#ifndef ABCD_SOURCE_SCOPE_FACTS_H_
#define ABCD_SOURCE_SCOPE_FACTS_H_

#include "abcd/analysis/semantic_validation.h"
#include "abcd/analysis/exception_locals.h"

namespace abcd::source {
using CaptureCell = std::variant<analysis::LocalCell, analysis::CapturedSlot>;
enum class ScopeError : std::uint8_t {
  kSite, kEnvironment, kCell, kInitialization, kWrites, kDomain
};
// Borrowed view of ONE freshly verified, unchanged semantic owner. The verifier
// owns sorted cell/store/barrier/guard indexes; no names or source text certify
// these facts. Keep all inputs alive and unchanged. Allocation IDs identify
// sites, not dynamic cell instances, especially on loop re-entry.
class NormalScopeFacts {
 public:
  NormalScopeFacts(const analysis::SemanticMethod& method, const analysis::SemanticProof& proof)
      : NormalScopeFacts(method, proof.environments, proof.locals, proof.defined_captures,
                   proof.closure_writes) {}
  NormalScopeFacts(const analysis::SemanticMethod& method,
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
  analysis::ClosureWrites Writes() const { return writes_; }

 private:
  const analysis::SemanticMethod& method_;
  const analysis::EnvironmentFlow& environments_;
  const analysis::LocalAccessPlan& locals_;
  const analysis::DefinedCaptures& defined_;
  analysis::ClosureWrites writes_;
};
// Same verified, unchanged exception owner. Before identifies ONE creation;
// original node IDs are never normal instruction indexes. All stores must pass
// VerifyExceptionInputs after applying locals.loads. Neither this borrowed view
// nor a successful CapturePlan closes locals.writes or child/source obligations.
struct ExceptionScopeFacts {
  const analysis::ExceptionFlow& flow;
  const analysis::EnvironmentFlow& environments;
  const analysis::ExceptionLocals& locals;
  analysis::ExceptionNodeId before;
  [[nodiscard]] std::variant<CaptureCell, ScopeError> Rebase(
      analysis::InstructionId site, analysis::CapturedSlot capture, analysis::CaptureDomain domain) const;
  [[nodiscard]] bool Matches(analysis::InstructionId site, const analysis::CreationBinding& creation) const;
  [[nodiscard]] std::uint64_t QueryWork() const;
  analysis::ClosureWrites Writes() const { return locals.writes; }
};
class ScopeFacts {
 public:
  ScopeFacts(const analysis::SemanticMethod& method, const analysis::SemanticProof& proof)
      : input_(NormalScopeFacts(method, proof)) {}
  ScopeFacts(const analysis::SemanticMethod& method, const analysis::EnvironmentFlow& environments,
      const analysis::LocalAccessPlan& locals, const analysis::DefinedCaptures& defined,
      analysis::ClosureWrites writes) : input_(NormalScopeFacts(method, environments, locals, defined, writes)) {}
  explicit ScopeFacts(ExceptionScopeFacts input) : input_(input) {}
  [[nodiscard]] std::variant<CaptureCell, ScopeError> Rebase(
      analysis::InstructionId site, analysis::CapturedSlot capture, analysis::CaptureDomain domain) const;
  [[nodiscard]] bool Matches(analysis::InstructionId site, const analysis::CreationBinding& creation) const;
  [[nodiscard]] std::uint64_t QueryWork() const;
  [[nodiscard]] analysis::ClosureWrites Writes() const;
 private:
  std::variant<NormalScopeFacts, ExceptionScopeFacts> input_;
};
}  // namespace abcd::source
#endif
