#include "function_segments.h"

#include <algorithm>
#include <limits>
#include <ostream>

#include "abcd/source/function_emission.h"
#include "capture_stream.h"

namespace abcd::source {
namespace {
[[noreturn]] void Invalid() { throw SourceFailure{SourceError::kInvalidEffect, {}}; }
[[noreturn]] void Resource() { throw SourceFailure{SourceError::kResourceLimit, {}}; }
GeneratedPosition Rebase(GeneratedPosition point, GeneratedPosition origin) {
  if (!point.line || !origin.line || point.byte > SIZE_MAX - origin.byte ||
      point.captures > SIZE_MAX - origin.captures || point.line - 1 > UINT32_MAX - origin.line ||
      (point.line == 1 && point.column > UINT32_MAX - origin.column)) Invalid();
  point.byte += origin.byte;
  point.captures += origin.captures;
  if (point.line == 1) point.column += origin.column;
  point.line += origin.line - 1;
  return point;
}
}  // namespace
void CheckFunctionSegment(const FunctionSegment& segment) {
  if (!segment.function || !segment.begin.line || segment.begin.line > segment.end.line ||
      (segment.begin.line == segment.end.line && segment.begin.column > segment.end.column) ||
      segment.begin.byte > segment.end.byte || segment.begin.captures != segment.end.captures ||
      segment.end.byte - segment.begin.byte != segment.function->Positions().bytes)
    Invalid();
  if (segment.function->Receipt().coverage != Coverage::kComplete ||
      ValidateRouteReceipt(segment.function->Receipt())) Invalid();
}
void CheckFunctionSegments(std::span<const FunctionSegment> segments,
                           std::size_t bytes, std::size_t captures) {
  for (const auto& segment : segments) {
    CheckFunctionSegment(segment);
    if (segment.end.byte > bytes || segment.end.captures > captures) Invalid();
  }
}
FunctionSegment RebaseFunctionSegment(const FunctionSegment& segment,
                                     const GeneratedPosition& origin) {
  CheckFunctionSegment(segment);
  FunctionSegment result{segment.function, Rebase(segment.begin, origin), Rebase(segment.end, origin)};
  CheckFunctionSegment(result);
  return result;
}
FunctionSegment BindFunctionSegment(const FunctionSegment& segment,
    const PrintedTemplate& source, std::span<const CaptureShift> prefix) {
  CheckFunctionSegment(segment);
  FunctionSegment result{segment.function,
      RelocateCapturePosition(source, prefix, segment.begin, {}),
      RelocateCapturePosition(source, prefix, segment.end, {})};
  CheckFunctionSegment(result);
  return result;
}
void AppendFunctionSegment(std::vector<FunctionSegment>& segments,
    const FunctionSegment& segment, std::size_t& remaining) {
  CheckFunctionSegment(segment);
  // Charge a shared payload for every retained occurrence conservatively. Never
  // spend the child preparation allowance again after it becomes parent-owned.
  const auto payload = segment.function->AllocatedBytes();
  if (payload > remaining) Resource();
  remaining -= payload;
  if (segments.size() == segments.capacity()) {
    const auto old = segments.capacity();
    const auto capacity = old == 0 ? 1 : old <= SIZE_MAX / 2 ? old * 2 : SIZE_MAX;
    if (capacity <= old || capacity > remaining / sizeof(FunctionSegment)) Resource();
    segments.reserve(capacity);
    if (segments.capacity() > remaining / sizeof(FunctionSegment)) Resource();
    remaining -= (segments.capacity() - old) * sizeof(FunctionSegment);
  }
  segments.push_back(segment);
}
void ForwardFunctionSegments(std::ostream& output,
    std::span<const FunctionSegment> segments, const GeneratedPosition* base) {
  if (segments.empty()) return;
  auto* sink = dynamic_cast<CaptureBuffer*>(output.rdbuf());
  GeneratedPosition origin;
  if (!sink || (!base && !sink->Position(origin))) Invalid();
  if (base) origin = *base;
  for (const auto& segment : segments)
    if (!sink->RetainFunction(RebaseFunctionSegment(segment, origin))) Invalid();
}
}  // namespace abcd::source
