#ifndef ABCD_SOURCE_STATIC_FIELDS_H_
#define ABCD_SOURCE_STATIC_FIELDS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "abcd/abc/method_body.h"
#include "abcd/abc/module_info.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/literal_graph.h"

namespace abcd::source {
struct StaticArray {
  std::vector<StaticAtom> elements;
};
using StaticValue = std::variant<StaticAtom, StaticArray, RecordNodeRef>;
struct StaticFieldOrigin {
  abc::EntityOffset method;
  abc::CodeOffset pc;
};
struct StaticField {
  std::u16string name;
  std::size_t value = 0;
  // Exact receiver-field definition, not the value's construction provenance.
  std::optional<StaticFieldOrigin> origin = {};
};
struct StaticFields {
  std::vector<StaticValue> values;
  std::vector<StaticField> fields;
  std::vector<RecordLiteralNode> nodes;
  [[nodiscard]] std::size_t AllocatedBytes() const;
};
enum class StaticFieldError {
  kCode,
  kFrame,
  kInstruction,
  kValue,
  kConstant,
  kResourceLimit,
  kRead,
  kFileChanged
};
struct StaticFieldFailure {
  StaticFieldError kind;
  abc::CodeOffset pc{};
};
using StaticFieldResult = std::variant<StaticFields, StaticFieldFailure>;

// Proves a zero-argument v7 or dynamic-12.0.6 initializer builds fresh
// literals, performs guarded imported constructions and defines fields on its
// own `this`, then returns undefined. The caller proves a single CALLTHIS0 on
// a fresh unescaped class. Arrays cannot be reused or mutated after
// publication. Each imported constructor read is consumed once; its result
// must reach a field before the next observable effect. The caller supplies
// the owning record module.
[[nodiscard]] StaticFieldResult RecoverStaticFields(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodInfo& method,
    std::size_t bytes, abc::StringBudget strings = {},
    const abc::ModuleInfo* module = nullptr);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_STATIC_FIELDS_H_
