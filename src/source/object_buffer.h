#ifndef ABCD_SOURCE_OBJECT_BUFFER_H_
#define ABCD_SOURCE_OBJECT_BUFFER_H_

#include <string>
#include <vector>

#include "abcd/analysis/object_creation.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
struct ObjectBuffer {
  abc::LiteralArray literal;
  std::vector<std::u16string> keys;
  std::vector<std::size_t> order;
  std::size_t allocated_bytes = 0;
};
using ObjectBufferResult = std::variant<ObjectBuffer, BodyEmitFailure>;
ObjectBufferResult ReadObjectBuffer(
    const analysis::BufferedObjectCreation& creation, abc::CodeOffset offset,
    const MethodConstants& constants, ObjectBufferBudget scratch_budget);
BodyEmitResult WriteObjectBuffer(const analysis::BufferedObjectCreation& creation,
                                 abc::CodeOffset offset,
                                 const MethodConstants& constants,
                                 std::ostream& output, EmitBudget output_budget,
                                 ObjectBufferBudget scratch_budget);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_OBJECT_BUFFER_H_
