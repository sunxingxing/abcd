#ifndef ABCD_SOURCE_OBJECT_BUFFER_H_
#define ABCD_SOURCE_OBJECT_BUFFER_H_

#include <optional>
#include <string>
#include <vector>

#include "abcd/analysis/object_creation.h"
#include "abcd/source/method_context.h"
#include "abcd/source/object_field.h"

namespace abcd::source {
struct ObjectBuffer {
  abc::LiteralArray literal;
  std::vector<std::u16string> keys;
  // Entry index of each key's value. A method key names its METHOD_AFFILIATE
  // entry (the preceding entry is the METHOD-family tag); data keys name the
  // single following entry. Only a methods-enabled read yields methods.
  std::vector<std::uint32_t> values;
  std::vector<std::size_t> order;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
using ObjectBufferResult = std::variant<ObjectBuffer, BodyEmitFailure>;
// methods=false keeps the primitive-only contract. methods=true also admits
// v7 key+METHOD*+METHOD_AFFILIATE triples; their function objects are NOT
// values here and require an indexed creation occurrence per method.
ObjectBufferResult ReadObjectBuffer(
    const analysis::BufferedObjectCreation& creation, abc::CodeOffset offset,
    const MethodConstants& constants, ObjectBufferBudget scratch_budget,
    bool methods = false);
[[nodiscard]] inline bool IsBufferMethod(const ObjectBuffer& buffer, std::size_t key) {
  return key < buffer.values.size() && buffer.values[key] < buffer.literal.entries.size() &&
      buffer.literal.entries[buffer.values[key]].tag == abc::LiteralTag::kMethodAffiliate;
}
BodyEmitResult WriteObjectBuffer(const analysis::BufferedObjectCreation& creation,
                                 abc::CodeOffset offset,
                                 const MethodConstants& constants,
                                 std::ostream& output, EmitBudget output_budget,
                                 ObjectBufferBudget scratch_budget,
                                 std::optional<ObjectField> field = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_OBJECT_BUFFER_H_
