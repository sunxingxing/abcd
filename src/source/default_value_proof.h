#ifndef ABCD_SOURCE_DEFAULT_VALUE_PROOF_H_
#define ABCD_SOURCE_DEFAULT_VALUE_PROOF_H_

#include <span>

#include "default_candidates.h"
#include "cell_plan.h"

namespace abcd::source {
struct DefaultOpPlacement {
  analysis::OpId operation;
  std::uint32_t parameter;
};
struct CapturedDefaultBinding {
  CapturedDefaultCandidate candidate;
  IncomingCell storage;
};
enum class DefaultProofError : std::uint8_t {
  kRevision, kShape, kControl, kParameter, kLiteral, kEffect, kEscapedValue,
  kResourceLimit, kWorkLimit
};
// Sealed IR-level proof of a literal or incoming-cell default prologue. It proves exact edges,
// operands, occurrence ownership and absence of escaping pre-default values.
// Input must already have passed normal-IR import/rewrite/use/dominance proof;
// this independently checks the proposed candidates, not the whole importer.
// It does NOT prove source names, parameter environments, arguments mapping or
// native callable syntax. Those obligations remain with signature construction.
class DefaultValueProof {
 public:
  DefaultValueProof(DefaultValueProof&&) noexcept = default;
  DefaultValueProof& operator=(DefaultValueProof&&) noexcept = default;
  [[nodiscard]] static std::variant<DefaultValueProof, DefaultProofError> Build(
      const analysis::ValueProgram& graph, const ParameterPlan& parameters,
      const DefaultCandidates& candidates, std::uint32_t length,
      analysis::SemanticBudget budget = {});
  // Explicit nonliteral path. Scope is checked against the original graph;
  // the residual owner must carry this incoming storage into signature binding.
  [[nodiscard]] static std::variant<DefaultValueProof, DefaultProofError> BuildCaptured(
      const analysis::ValueProgram&, const analysis::ValueEnvironments&,
      const CellPlan&, const ParameterPlan&, const DefaultCandidates&,
      std::uint32_t length, analysis::SemanticBudget budget = {});
  [[nodiscard]] const std::optional<CapturedDefaultBinding>& Capture() const { return capture_; }
  [[nodiscard]] std::span<const DefaultCandidate> Defaults() const { return defaults_; }
  [[nodiscard]] std::span<const DefaultOpPlacement> Operations() const { return operations_; }
  [[nodiscard]] analysis::WorkBlockId BodyEntry() const { return body_; }
  [[nodiscard]] analysis::IrRevision Revision() const { return revision_; }
  [[nodiscard]] analysis::CfgRevision ControlRevision() const { return cfg_revision_; }
  [[nodiscard]] std::size_t AllocatedBytes() const { return allocated_bytes_; }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  DefaultValueProof() = default;
  static std::variant<DefaultValueProof, DefaultProofError> BuildChecked(
      const analysis::ValueProgram&, const ParameterPlan&, const DefaultCandidates&,
      std::uint32_t, std::optional<CapturedDefaultBinding>, analysis::SemanticBudget);
  std::optional<CapturedDefaultBinding> capture_;
  std::vector<DefaultCandidate> defaults_;
  std::vector<DefaultOpPlacement> operations_;
  analysis::WorkBlockId body_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_revision_;
  std::size_t allocated_bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
