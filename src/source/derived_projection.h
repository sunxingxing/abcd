#ifndef ABCD_SOURCE_DERIVED_PROJECTION_H_
#define ABCD_SOURCE_DERIVED_PROJECTION_H_
#include "function_file.h"
namespace abcd::source {
// Caller-admitted same immutable file/index/lookup used to resolve the owner.
// Never a source sidecar or caller-supplied preservation certificate.
struct DerivedCaptureInput {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  const abc::MethodLookup& lookup;
};
struct ReceiverInitialization {
  CellId cell;
  analysis::InstructionId store;
  bool operator==(const ReceiverInitialization&) const = default;
};
enum class DerivedConstructorForm : std::uint8_t { kExplicit, kImplicit, kInvocationRest };
struct DerivedProjection {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  analysis::OpId call;
  DerivedConstructorForm form = DerivedConstructorForm::kExplicit;
  std::vector<analysis::OpId> forwarding_calls;
  std::vector<analysis::OpId> guards;
  // Allocated cells whose every direct store is this invocation's super
  // result, with all descendant captures independently proved read-only.
  // Capture binding still owes initialization before any child can observe it.
  std::vector<CellId> receiver_cells;
  // The common DAG rank proves no non-receiver store can execute after this
  // actual super store. A creation independently proves store dominance; an
  // incomparable write may require a finer cell snapshot than this suffix fact.
  std::vector<ReceiverInitialization> receiver_initializations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class DerivedProjectionError : std::uint8_t {
  kOwner, kProtocol, kOrigin, kOperand, kResourceLimit, kWorkLimit, kRevision, kCoverage,
  kForwardingContract, kForwardingFrame, kForwardingFrameChanged,
  kForwardingHandlers, kForwardingArguments, kForwardingNewTarget,
  kForwardingParameters, kForwardingRepeated, kForwardingMissing, kForwardingReturn
};
// Same verified input/work pair, with a genuine derived creation owner. Reprove
// original initialization and check actual working guard inputs before allowing
// a source consumer to omit them. Range and spread share initialization; spread
// also requires the actual working callable operand to be this invocation's
// constructor. This does not authorize spread-array expansion, heritage emission,
// receiver substitution, or changing constructor returns. Optional same-file
// capture input independently proves preservation through actual descendants;
// absent input retains conservative refusal after an unproved creation. Caller admits
// the retained input/work pair; original proof and projection use separate bounded
// passes with disjoint scratch and the supplied per-pass work limit.
[[nodiscard]] std::variant<DerivedProjection, DerivedProjectionError> ProjectDerivedConstructor(
    const FileFunction&, const FunctionContext&, analysis::SemanticBudget = {},
    const DerivedCaptureInput* captures = nullptr);
// Recheck against the current same-owner body/working graph before consuming a
// cached projection. Revision equality alone is insufficient: IDs and complete
// guard coverage must match a fresh protocol proof.
[[nodiscard]] std::optional<DerivedProjectionError> VerifyDerivedProjection(
    const FileFunction&, const FunctionContext&, const DerivedProjection&,
    analysis::SemanticBudget = {}, const DerivedCaptureInput* captures = nullptr);
}
#endif
