#ifndef ABCD_SOURCE_CELL_DEFAULT_PROOF_H_
#define ABCD_SOURCE_CELL_DEFAULT_PROOF_H_

#include "cell_defaults.h"
#include "default_value_proof.h"

namespace abcd::source {
enum class CellDefaultProofError : std::uint8_t {
  kRevision, kShape, kControl, kParameter, kCell, kLiteral, kEffect, kEscape,
  kResourceLimit, kWorkLimit
};
// Sealed proof of a cell-backed constant default prefix. Original graph,
// environments and parameter identities must be unchanged, verified inputs.
// Does not prove source parameter environments or authorize publication.
class CellDefaultProof {
 public:
  CellDefaultProof(CellDefaultProof&&) noexcept = default;
  CellDefaultProof& operator=(CellDefaultProof&&) noexcept = default;
  [[nodiscard]] static std::variant<CellDefaultProof, CellDefaultProofError> Build(
      const analysis::ValueProgram& graph, const analysis::ValueEnvironments& environments,
      const CellPlan& cells, const ParameterPlan& parameters, const CellDefaults& candidates,
      std::uint32_t length, analysis::SemanticBudget budget = {});
  std::span<const CellDefault> Defaults() const { return defaults_; }
  std::span<const DefaultOpPlacement> Operations() const { return operations_; }
  analysis::IrRevision Revision() const { return revision_; }
  analysis::CfgRevision ControlRevision() const { return cfg_revision_; }
  analysis::WorkBlockId BodyEntry() const { return body_; }
  std::size_t AllocatedBytes() const { return allocated_bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  CellDefaultProof() = default;
  std::vector<CellDefault> defaults_;
  std::vector<DefaultOpPlacement> operations_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_revision_;
  analysis::WorkBlockId body_;
  std::size_t allocated_bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
