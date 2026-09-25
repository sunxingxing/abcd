#ifndef ABCD_SOURCE_CAPTURE_POSITIONS_H_
#define ABCD_SOURCE_CAPTURE_POSITIONS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct CaptureShift {
  std::size_t bytes = 0;
  std::size_t captures = 0;
};
// Prefix contains cumulative byte and unresolved capture shifts, starting at zero,
// one entry per capture plus its sentinel. Insertions contain no newlines.
// Throws SourceFailure for invalid coordinates or arithmetic overflow.
GeneratedPosition RelocateCapturePosition(
    const PrintedTemplate& source, std::span<const CaptureShift> prefix,
    GeneratedPosition position, abc::CodeOffset pc);
}  // namespace abcd::source
#endif
