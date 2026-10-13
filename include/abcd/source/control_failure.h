#ifndef ABCD_SOURCE_CONTROL_FAILURE_H_
#define ABCD_SOURCE_CONTROL_FAILURE_H_
#include "abcd/abc/method_body.h"
#include <optional>
namespace abcd::source {
// IDs are in the failing control projection; origin tags prevent synthetic
// units from being mistaken for instructions. Missing PCs are intentional.
enum class ControlOriginKind : std::uint8_t { kBlock, kPhase, kTry, kTryExit, kLoop, kLoopTransfer };
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
struct ControlOwnershipEvidence {
  ControlLocation node;
  std::optional<std::uint32_t> arc; // exact incoming projection arc, if traversed
  ControlRegionBounds current, previous;
  bool operator==(const ControlOwnershipEvidence&) const = default;
};
} // namespace abcd::source
#endif
