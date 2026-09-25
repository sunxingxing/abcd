#ifndef ABCD_SOURCE_CAPTURE_POSITIONS_H_
#define ABCD_SOURCE_CAPTURE_POSITIONS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Prefix contains cumulative inserted byte lengths, starting with zero, and
// one entry per capture plus its sentinel. Insertions contain no newlines.
// Throws SourceFailure for invalid coordinates or arithmetic overflow.
GeneratedPosition RelocateCapturePosition(
    const PrintedTemplate& source, std::span<const std::size_t> prefix,
    GeneratedPosition position, abc::CodeOffset pc);
}  // namespace abcd::source
#endif
