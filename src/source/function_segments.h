#ifndef ABCD_SOURCE_FUNCTION_SEGMENTS_H_
#define ABCD_SOURCE_FUNCTION_SEGMENTS_H_

#include "capture_positions.h"

namespace abcd::source {
// Typed coordinate transport only. Throws SourceFailure on malformed envelopes,
// capture insertion inside a verified method, or arithmetic/resource failure.
void CheckFunctionSegment(const FunctionSegment& segment);
void CheckFunctionSegments(std::span<const FunctionSegment> segments,
                           std::size_t bytes, std::size_t captures);
FunctionSegment RebaseFunctionSegment(const FunctionSegment& segment,
                                     const GeneratedPosition& origin);
FunctionSegment BindFunctionSegment(const FunctionSegment& segment,
    const PrintedTemplate& source, std::span<const CaptureShift> prefix);
void AppendFunctionSegment(std::vector<FunctionSegment>& segments,
    const FunctionSegment& segment, std::size_t& remaining);
void ForwardFunctionSegments(std::ostream& output,
    std::span<const FunctionSegment> segments,
    const GeneratedPosition* origin = nullptr);
}  // namespace abcd::source
#endif
