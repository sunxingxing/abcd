#ifndef ABCD_SOURCE_FOR_OF_ITERATION_H_
#define ABCD_SOURCE_FOR_OF_ITERATION_H_
#include "for_of_identity.h"
namespace abcd::source {
enum class ForOfIterationError : std::uint8_t {
  kInput, kIdentity, kEntry, kAdvance, kDone, kBinding, kActivation,
  kCorrespondence, kAcquisitionSemantics, kResourceLimit, kWorkLimit
};
struct ForOfIterationFailure {
  ForOfIterationError kind;
  std::optional<IteratorProtocolId> protocol;
  std::optional<analysis::OpId> operation;
  std::optional<ControlNodeId> node;
  std::optional<ControlArcId> edge;
  std::optional<analysis::ValueId> value;
};
struct ForOfIteration {
  IteratorProtocolId protocol;
  ControlNodeId header, advance, value, decision;
  ControlArcId success, exhausted;
  std::optional<LoopId> loop;
  bool operator==(const ForOfIteration&) const = default;
};
// A real scalar definition, not a source variable selection. Body statements
// and all other writers stay ordinary. The tree adapter must choose an actual
// binding and prove scope/activation and its live-out before native admission.
struct ForOfKeyDefinition {
  IteratorProtocolId protocol;
  analysis::OpId operation;
  analysis::ValueId value;
  analysis::SsaValueId original;
  std::optional<abc::RegisterId> reg; // absent denotes acc
  bool operator==(const ForOfKeyDefinition&) const = default;
};
struct ForOfKeyPhi {
  IteratorProtocolId protocol;
  analysis::ValueId phi;
  analysis::UseId input;
  analysis::EdgeId edge;
  analysis::ValueId value;
  bool operator==(const ForOfKeyPhi&) const = default;
};
// Standalone evidence, NOT evaluation or tree admission. Count states are
// recomputed over B's immutable projection; source writes/phi inputs retain
// original identities. No symbols/temporaries or source expressions are made.
struct ForOfIterationProof {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ForOfIteration> iterations;
  std::vector<ForOfKeyDefinition> definitions;
  std::vector<ForOfKeyPhi> phis;
  std::size_t allocated_bytes=0, peak_bytes=0;
  std::uint64_t work=0;
};
[[nodiscard]] std::variant<ForOfIterationProof,ForOfIterationFailure> ProveForOfIterations(
    const abc::MethodBody&, const analysis::ValueProgram&, const analysis::PhasedValueInput&,
    const ExceptionPlan&, const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    SourceContract, const ForOfIdentityIndex&, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfIterations(
    const abc::MethodBody&, const analysis::ValueProgram&, const analysis::PhasedValueInput&,
    const ExceptionPlan&, const ExceptionControl&, const VerifiedProtocolRegionOwnership&,
    SourceContract, const ForOfIdentityIndex&, const ForOfIterationProof&, analysis::SemanticBudget = {});
}
#endif
