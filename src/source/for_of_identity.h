#ifndef ABCD_SOURCE_FOR_OF_IDENTITY_H_
#define ABCD_SOURCE_FOR_OF_IDENTITY_H_

#include "exception_control.h"
#include "close_entries.h"

namespace abcd::source {
enum class ForOfIdentityError : std::uint8_t {
  kAcquisitionSemantics, kIdentity, kEscape, kCachedNext, kAdvance,
  kObjectCheck, kDone, kValueOrder,
  kExceptionCorrespondence, kCorrespondence, kResourceLimit, kWorkLimit
};
struct ForOfIdentityFailure {
  ForOfIdentityError kind;
  std::optional<analysis::OpId> acquisition, operation;
  std::optional<analysis::ValueId> value;
  std::optional<analysis::UseId> use;
  std::optional<analysis::EdgeId> edge;
  bool operator==(const ForOfIdentityFailure&) const = default;
};
enum class ForOfRole : std::uint8_t {
  kAcquisition, kCachedNext, kAdvance, kObjectCheck, kDone, kDecision,
  kValue, kTransport, kClose
};
struct ForOfMember {
  IteratorProtocolId protocol;
  ForOfRole role;
  bool operator==(const ForOfMember&) const = default;
};
struct ForOfLoop {
  IteratorProtocolId protocol;
  analysis::OpId acquisition, next, advance, check, done, decision, value;
  analysis::ValueId iterator, cached_next, result, key;
  analysis::CompletionEdge exhausted;
  analysis::ExceptionNodeId success;
  std::optional<analysis::ExceptionNodeId> header;
  bool operator==(const ForOfLoop&) const = default;
};
// Identity/use index only, NOT a ForOfPlan or evaluation admission certificate.
// Advance multiplicity, source binding commits and source event ownership remain
// unproved; callers must not use this index to skip ordinary evaluation.
// These are original operations/edges, never source expressions. The advance
// result object differs from the value extracted on the successful done edge.
// The close certificate owns close state; it never owns ordinary loop-body ops.
struct ForOfIdentityIndex {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ForOfLoop> loops;
  std::vector<std::optional<ForOfMember>> operations, values, uses;
  std::size_t allocated_bytes = 0, peak_bytes = 0;
  std::uint64_t work = 0;
};
// Requires the original input and sealed close ownership, not scan candidates.
// Shared SCC identities and exceptional dominance; bounded indexed work. No
// symbols, temporary planning, control mutation or source publication occurs.
[[nodiscard]] std::variant<ForOfIdentityIndex, ForOfIdentityFailure> IndexForOfIdentities(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const ExceptionControl&,
    const VerifiedProtocolRegionOwnership&, SourceContract,
    analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t, ForOfIdentityFailure> VerifyForOfIdentityIndex(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&, const ExceptionControl&,
    const VerifiedProtocolRegionOwnership&, SourceContract, const ForOfIdentityIndex&,
    analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
