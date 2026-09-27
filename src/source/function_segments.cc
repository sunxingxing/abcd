#include "function_segments.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <type_traits>

#include "abcd/source/function_emission.h"
#include "family_emission.h"
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
FunctionSegmentView ViewFunctionSegment(const FunctionSegment& segment) {
  return std::visit([](const auto& payload) -> FunctionSegmentView {
    if (!payload) Invalid();
    using T = std::remove_cvref_t<decltype(*payload)>;
    if constexpr (std::is_same_v<T, FunctionEmission>) {
      return {payload->Function().Tree(), payload->Positions(), payload->Receipt(),
          payload->Function().ClassCreation(), nullptr, nullptr, payload->Positions().bytes, payload->AllocatedBytes()};
    } else {
      static_assert(std::is_same_v<T, FamilyEmission>);
      const auto& family = payload->Family().Family();
      const auto root = family.root.value;
      return {family.members[root].tree, payload->Positions().members[root], payload->Receipts()[root],
          payload->Family().ClassCreation(), payload.get(), &*family.members[root].sources,
          payload->Positions().bytes, payload->AllocatedBytes()};
    }
  }, segment.function);
}
void CheckFunctionSegment(const FunctionSegment& segment) {
  const auto view = ViewFunctionSegment(segment);
  if (!segment.begin.line || segment.begin.line > segment.end.line ||
      (segment.begin.line == segment.end.line && segment.begin.column > segment.end.column) ||
      segment.begin.byte > segment.end.byte || segment.begin.captures != segment.end.captures ||
      segment.end.byte - segment.begin.byte != view.bytes)
    Invalid();
  if (view.receipt.coverage != Coverage::kComplete || ValidateRouteReceipt(view.receipt)) Invalid();
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
  // Transport changes coordinates, not lexical enclosure. A new wrapper or
  // binding set must go through source printing with its actual GlobalScope;
  // a complete receipt is not permission to embed under new shadowing names.
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
  const auto payload = ViewFunctionSegment(segment).allocated_bytes;
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
