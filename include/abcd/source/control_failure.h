#ifndef ABCD_SOURCE_CONTROL_FAILURE_H_
#define ABCD_SOURCE_CONTROL_FAILURE_H_
#include "abcd/abc/method_body.h"
#include <optional>
namespace abcd::source {
// IDs are in the failing control projection; origin tags prevent synthetic
// units from being mistaken for instructions. Missing PCs are intentional.
enum class ControlOriginKind : std::uint8_t { kBlock, kPhase, kTry, kTryExit, kLoop, kLoopTransfer, kProtocolClose };
struct ControlLocation {
  std::uint32_t node = 0;
  ControlOriginKind origin = ControlOriginKind::kBlock;
  std::uint32_t source = 0;
  std::optional<abc::CodeOffset> pc, working_pc;
  bool operator==(const ControlLocation&) const = default;
};
struct ControlRegionBounds {
  ControlLocation first;
  std::optional<ControlLocation> stop; // absent is the virtual exit
  bool operator==(const ControlRegionBounds&) const = default;
};
enum class ControlOwnershipError : std::uint8_t { kDuplicate, kEntry };
enum class DecisionContinuationError : std::uint8_t { kPostdominator, kPrefix };
struct DecisionContinuationEvidence {
  ControlLocation entry, predicate, shared, fallback, old_join, enclosing_stop;
  // A real location on the incompatible continuation, never an invented PC.
  ControlLocation witness;
  bool operator==(const DecisionContinuationEvidence&) const = default;
};
struct ControlOwnershipEvidence {
  ControlLocation node;
  std::optional<std::uint32_t> arc; // exact incoming projection arc, if traversed
  ControlRegionBounds current, previous;
  std::optional<DecisionContinuationEvidence> decision = {};
  bool operator==(const ControlOwnershipEvidence&) const = default;
};
// All projection adapters must translate the full witness, including decision
// endpoints; synthetic nodes retain their typed identity and have no fake PC.
template<class F> void MapControlLocations(ControlOwnershipEvidence& evidence, F locate) {
  locate(evidence.node); locate(evidence.current.first); locate(evidence.previous.first);
  if (evidence.current.stop) locate(*evidence.current.stop);
  if (evidence.previous.stop) locate(*evidence.previous.stop);
  if (evidence.decision) {
    auto& d = *evidence.decision;
    locate(d.entry); locate(d.predicate); locate(d.shared); locate(d.fallback);
    locate(d.old_join); locate(d.enclosing_stop); locate(d.witness);
  }
}
} // namespace abcd::source
#endif
