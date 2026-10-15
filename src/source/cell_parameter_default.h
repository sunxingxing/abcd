#ifndef ABCD_SOURCE_CELL_PARAMETER_DEFAULT_H_
#define ABCD_SOURCE_CELL_PARAMETER_DEFAULT_H_
#include "cell_plan.h"
#include "parameter_plan.h"
#include "abcd/source/cell_parameter_default_failure.h"
namespace abcd::source {
// Four identities: raw entry, initialized source formal (parameter ordinal),
// allocated body cell, and incoming default source. No source symbol is allocated.
struct CellParameterDefault {
  std::uint32_t parameter = 0;
  analysis::ValueId entry;
  CellId local, outer;
  ParameterCellIdentity destination;
  DefaultCaptureIdentity source;
  analysis::OpId initial_store, test, comparison, branch, read, guard, fallback_store;
  analysis::EdgeId missing, provided;
  analysis::WorkBlockId join;
  bool operator==(const CellParameterDefault&) const = default;
};
struct CellParameterDefaultCost { std::size_t allocated_bytes = 0; std::uint64_t work = 0; };
// Pure original-IR analysis. A certificate proves only the private initialization
// relation; signature scope, family binding and publication remain gated.
class CellParameterDefaultProof {
 public:
  CellParameterDefaultProof(CellParameterDefaultProof&&) noexcept = default;
  CellParameterDefaultProof& operator=(CellParameterDefaultProof&&) noexcept = default;
  static std::variant<CellParameterDefaultProof, CellParameterDefaultFailure> Build(
      const analysis::ValueProgram&, const analysis::ValueEnvironments&, const CellPlan&,
      const ParameterPlan&, std::uint32_t length, analysis::SemanticBudget = {});
  std::span<const CellParameterDefault> Defaults() const { return defaults_; }
  const CellParameterDefaultCost& Cost() const { return cost_; }
  analysis::IrRevision Revision() const { return revision_; }
  analysis::CfgRevision ControlRevision() const { return cfg_revision_; }
 private:
  CellParameterDefaultProof() = default;
  std::vector<CellParameterDefault> defaults_;
  CellParameterDefaultCost cost_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_revision_;
};
// Rebuilds all indexes from immutable input, checks proposed records rather than
// trusting discovery flags. O((V+E+U) log(V+E+U)); charged scratch before allocation.
std::variant<CellParameterDefaultCost, CellParameterDefaultFailure> VerifyCellParameterDefaults(
    const analysis::ValueProgram&, const analysis::ValueEnvironments&, const CellPlan&,
    const ParameterPlan&, std::uint32_t length, std::span<const CellParameterDefault>,
    analysis::SemanticBudget = {});
CellParameterDefaultDiagnostic DiagnoseCellParameterDefaults(
    const analysis::ValueProgram&, const analysis::ValueEnvironments&, const CellPlan&,
    const ParameterPlan&, std::uint32_t length, abc::EntityOffset, analysis::SemanticBudget);
}
#endif
