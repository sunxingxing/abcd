#ifndef ABCD_SOURCE_RECEIVER_COPY_H_
#define ABCD_SOURCE_RECEIVER_COPY_H_

#include "abcd/source/scalar_expression.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::source {
// Whole-method SSA certificate for the first direct receiver copy in the
// pure entry prefix. Reject writes, phis and reads of an uninitialized value.
std::optional<abc::RegisterId> FindReceiverCopy(
    const analysis::SemanticMethod& method, abc::RegisterId receiver);
void BindReceiverCopy(analysis::ScalarExpression& expression,
                      abc::RegisterId copy, abc::RegisterId receiver);
}  // namespace abcd::source
#endif
