#ifndef ABCD_SOURCE_SCOPE_FACTS_H_
#define ABCD_SOURCE_SCOPE_FACTS_H_

#include "abcd/analysis/semantic_validation.h"
#include "abcd/analysis/exception_locals.h"

namespace abcd::source {
class FunctionContext;
// Borrowed genuine constructor/buffer-member context at DEFINECLASSWITHBUFFER.
// Defined members retain their real FunctionCreation binding instead. This is
// an original capture point, never a fabricated DEFINEFUNC for a class body.
struct ClassCaptureSite {
  const FunctionContext& owner;
  analysis::EnvironmentRef environment;
};
using CaptureSite = std::variant<analysis::CreationBinding, ClassCaptureSite>;
[[nodiscard]] std::optional<abc::CodeOffset> CapturePc(const CaptureSite& site);
[[nodiscard]] abc::MethodPosition CapturePosition(const CaptureSite& site);
[[nodiscard]] const analysis::ClassCreation* CaptureClassCreation(const ClassCaptureSite& site);
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
  NormalScopeFacts(const analysis::SemanticMethod& method, const analysis::SemanticProof& proof,
      std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> module_prefix = {},
      bool source_tdz = false)
      : NormalScopeFacts(method, proof.environments, proof.locals, proof.defined_captures,
                   proof.closure_writes) { module_prefix_ = module_prefix; source_tdz_ = source_tdz; }
  NormalScopeFacts(const analysis::SemanticMethod& method,
      const analysis::EnvironmentFlow& environments, const analysis::LocalAccessPlan& locals,
      const analysis::DefinedCaptures& defined, analysis::ClosureWrites writes)
      : method_(method), environments_(environments), locals_(locals), defined_(defined), writes_(writes) {}
  // A Guarded use of a local cell that is not yet initialized at creation is
  // admitted only as a deferred TDZ initialization: allocation, creation and
  // the cell's first store share one block in that order. `deferred` then
  // receives that store; the owner must declare the binding exactly there.
  // Under the approved source TDZ premise (CaptureReads, language-source.md)
  // a Language read is deferred by the same rule: it is a source identifier
  // read of the binding that the owner declares at that store.
  [[nodiscard]] std::variant<CaptureCell, ScopeError> Rebase(
      analysis::InstructionId creation, analysis::CapturedSlot capture,
      analysis::CaptureDomain requirement,
      std::optional<analysis::InstructionId>* deferred = nullptr) const;
  [[nodiscard]] bool Matches(analysis::InstructionId site,
      const analysis::CreationBinding& creation) const;
  [[nodiscard]] bool Matches(analysis::InstructionId site, const ClassCaptureSite& creation) const;
  // Conservative logarithmic comparison/query bound; batch users debit this
  // before each query. Environment ancestry has a fixed uint32 depth domain.
  [[nodiscard]] std::uint64_t QueryWork() const;
  analysis::ClosureWrites Writes() const { return writes_; }

 private:
  std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> module_prefix_;
  const analysis::SemanticMethod& method_;
  const analysis::EnvironmentFlow& environments_;
  const analysis::LocalAccessPlan& locals_;
  const analysis::DefinedCaptures& defined_;
  analysis::ClosureWrites writes_;
  bool source_tdz_ = false;
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
  [[nodiscard]] bool Matches(analysis::InstructionId site, const ClassCaptureSite& creation) const;
  [[nodiscard]] std::uint64_t QueryWork() const;
  analysis::ClosureWrites Writes() const { return locals.writes; }
};
class ScopeFacts {
 public:
  ScopeFacts(const analysis::SemanticMethod& method, const analysis::SemanticProof& proof,
      std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> module_prefix = {},
      bool source_tdz = false)
      : input_(NormalScopeFacts(method, proof, module_prefix, source_tdz)) {}
  ScopeFacts(const analysis::SemanticMethod& method, const analysis::EnvironmentFlow& environments,
      const analysis::LocalAccessPlan& locals, const analysis::DefinedCaptures& defined,
      analysis::ClosureWrites writes) : input_(NormalScopeFacts(method, environments, locals, defined, writes)) {}
  explicit ScopeFacts(ExceptionScopeFacts input) : input_(input) {}
  [[nodiscard]] std::variant<CaptureCell, ScopeError> Rebase(
      analysis::InstructionId site, analysis::CapturedSlot capture, analysis::CaptureDomain domain,
      std::optional<analysis::InstructionId>* deferred = nullptr) const;
  [[nodiscard]] bool Matches(analysis::InstructionId site, const CaptureSite& creation) const;
  [[nodiscard]] std::uint64_t QueryWork() const;
  [[nodiscard]] analysis::ClosureWrites Writes() const;
 private:
  std::variant<NormalScopeFacts, ExceptionScopeFacts> input_;
};
}  // namespace abcd::source
#endif
