#ifndef ABCD_SOURCE_FOR_IN_EMISSION_H_
#define ABCD_SOURCE_FOR_IN_EMISSION_H_
#include "exception_control.h"
#include "for_in_events.h"
#include "exception_forwarding.h"
namespace abcd::source {
struct EvaluationPlan;
struct PhysicalCatch {
  ForInExceptionExitId exit;
  bool operator==(const PhysicalCatch&) const = default;
};
// Per original throwing entry, not a permission to erase a shared handler.
enum class ForwardingOwner : std::uint8_t { kEnumeration, kProtocolProjection };
struct ForwardedExceptionRoute {
  ForInExceptionExitId exit;
  analysis::WorkBlockId handler;
  analysis::ValueId payload;
  std::optional<analysis::ValueHandler> destination;
  ForwardingOwner owner = ForwardingOwner::kEnumeration;
  bool operator==(const ForwardedExceptionRoute&) const = default;
};
using EnumerationException = std::variant<PhysicalCatch, ForwardedExceptionRoute>;
struct ForInEmissionLoop {
  LoopId loop;
  ControlId decision;
  std::optional<ControlId> body;
  EnumerationException acquisition;
  bool exhaustion_only = true;
  bool operator==(const ForInEmissionLoop&) const = default;
};
// A borrowed control interpretation. Original nodes, arcs and LoopIds stay
// immutable. Handler removal requires a census of ALL original throw entries.
struct EnumerationContinuation {
  std::optional<ControlId> next;
  bool operator==(const EnumerationContinuation&) const = default;
};
struct ForInEmissionView {
  std::vector<std::optional<EnumerationContinuation>> continuations;
  std::vector<ForInEmissionLoop> loops;
  std::vector<std::optional<std::uint32_t>> wrappers; // LoopId -> protocol
  std::vector<std::optional<std::uint32_t>> forwarded_regions; // TryId -> protocol
  std::vector<std::optional<std::uint32_t>> forwarded_operations;
  std::vector<std::optional<std::uint32_t>> forwarded_blocks;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
std::variant<ForInEmissionView, ForInFailure> BuildForInEmissionView(
    const analysis::ValueProgram&, const ExceptionPlan&, const ExceptionControl&,
    const ForInPlan&, const ForInEvents&, analysis::SemanticBudget = {},
    const ProtocolForwarding* projected = nullptr);
std::variant<std::uint64_t, ForInFailure> VerifyForInCopyState(
    const analysis::ValueProgram&, const EvaluationPlan&, const ForInEmissionView&,
    analysis::SemanticBudget = {});
std::variant<std::uint64_t, ForInFailure> VerifyForInEmissionView(
    const analysis::ValueProgram&, const ExceptionPlan&, const ExceptionControl&,
    const ForInPlan&, const ForInEvents&, const ForInEmissionView&,
    analysis::SemanticBudget = {}, const ProtocolForwarding* projected = nullptr);
}
#endif
