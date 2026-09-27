#ifndef ABCD_SOURCE_FUNCTION_SEGMENTS_H_
#define ABCD_SOURCE_FUNCTION_SEGMENTS_H_

#include "capture_positions.h"
#include "abcd/source/function_emission.h"

namespace abcd::source {
struct InstructionSources;
// Borrowed root view for either sealed payload. A family remains one envelope;
// nested positions/receipts stay in the shared owner during coordinate moves.
struct FunctionSegmentView {
  const FunctionTree& tree;
  const PrintedFunction& positions;
  const MethodRouteReceipt& receipt;
  const analysis::ClassMethodSite* creation;
  const FamilyEmission* family;
  const InstructionSources* sources;
  std::size_t bytes;
  std::size_t allocated_bytes;
};
FunctionSegmentView ViewFunctionSegment(const FunctionSegment& segment);
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
