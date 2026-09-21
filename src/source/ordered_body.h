#ifndef ABCD_SOURCE_ORDERED_BODY_H_
#define ABCD_SOURCE_ORDERED_BODY_H_

#include "abcd/analysis/accumulator_flow.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
// Private rendering boundary. Graph/state/order must be validated together by
// WriteAcyclicBody. Shares instruction emission with WriteLinearBody.
BodyEmitResult WriteOrderedBody(const abc::MethodBody& body,
                                const analysis::NormalFlow& graph,
                                const analysis::AccumulatorFlow& flow,
                                ScalarNames names, std::ostream& output,
                                EmitBudget budget,
                                const MethodConstants* constants);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ORDERED_BODY_H_
