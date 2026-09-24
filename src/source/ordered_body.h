#ifndef ABCD_SOURCE_ORDERED_BODY_H_
#define ABCD_SOURCE_ORDERED_BODY_H_

#include "abcd/analysis/accumulator_flow.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
struct BranchSourceFacts;
// Private rendering boundary. Graph/state/order must be validated together by
// WriteAcyclicBody/WriteBlockLoopBody. Only self edges may repeat blocks;
// accumulator entries include the backedge fixed point. Shares instruction
// emission with WriteLinearBody.
BodyEmitResult WriteOrderedBody(const abc::MethodBody& body,
                                const analysis::NormalFlow& graph,
                                const analysis::AccumulatorFlow& flow,
                                ScalarNames names, std::ostream& output,
                                EmitBudget budget,
                                const MethodConstants* constants,
                                ObjectBufferBudget object_budget,
                                const BranchSourceFacts* facts = nullptr);
// A proof-owned straight-line prefix/suffix; the caller preflights the full
// staged expression before publication. first indexes unchanged body facts.
BodyEmitResult WriteBranchSegment(
    std::span<const abc::BodyInstruction> instructions, std::size_t first,
    bool initialized, bool terminal, ScalarNames names,
    std::ostream& output, EmitBudget budget,
    const MethodConstants* constants, const BranchSourceFacts& facts);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ORDERED_BODY_H_
