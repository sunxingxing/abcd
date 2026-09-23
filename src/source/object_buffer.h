#ifndef ABCD_SOURCE_OBJECT_BUFFER_H_
#define ABCD_SOURCE_OBJECT_BUFFER_H_

#include "abcd/analysis/object_creation.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
BodyEmitResult WriteObjectBuffer(const analysis::BufferedObjectCreation& creation,
                                 abc::CodeOffset offset,
                                 const MethodConstants& constants,
                                 std::ostream& output, EmitBudget output_budget,
                                 ObjectBufferBudget scratch_budget);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_OBJECT_BUFFER_H_
