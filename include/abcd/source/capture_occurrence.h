#ifndef ABCD_SOURCE_CAPTURE_OCCURRENCE_H_
#define ABCD_SOURCE_CAPTURE_OCCURRENCE_H_

#include "abcd/analysis/creation_binding.h"

namespace abcd::source {
// Diagnostic origin of a requirement, retained across family forwarding. An
// entry requirement has no instruction; never borrow its creator's PC.
struct CaptureOrigin {
  abc::EntityOffset method;
  std::optional<abc::CodeOffset> pc;
  std::optional<abc::CodeOffset> working_pc;
  bool operator==(const CaptureOrigin&) const = default;
};
struct CaptureOccurrence {
  std::uint32_t ordinal;
  analysis::CapturedSlot cell;
  analysis::CaptureDomain requested;
  std::optional<CaptureOrigin> source;
  bool operator==(const CaptureOccurrence& other) const {
    return ordinal == other.ordinal && cell.depth == other.cell.depth &&
        cell.slot.value == other.cell.slot.value && requested == other.requested && source == other.source;
  }
};
}  // namespace abcd::source
#endif
