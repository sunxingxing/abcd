#ifndef ABCD_SOURCE_STATIC_FIELDS_H_
#define ABCD_SOURCE_STATIC_FIELDS_H_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"

namespace abcd::source {
struct StaticUndefined {};
struct StaticNull {};
struct StaticFloat {
  std::uint64_t bits;
};
using StaticAtom = std::variant<StaticUndefined, StaticNull, bool,
                                std::int32_t, StaticFloat, std::u16string>;
struct StaticArray {
  std::vector<StaticAtom> elements;
};
using StaticValue = std::variant<StaticAtom, StaticArray>;
struct StaticField {
  std::u16string name;
  std::size_t value = 0;
};
struct StaticFields {
  std::vector<StaticValue> values;
  std::vector<StaticField> fields;
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

// Proves a zero-argument v7 or dynamic-12.0.6 method performs only constant
// DefineField writes on its own `this`, then returns undefined. The caller
// proves a single CALLTHIS0 on a fresh unescaped class. Arrays reused by
// multiple fields are rejected until source identity can be preserved.
[[nodiscard]] StaticFieldResult RecoverStaticFields(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodInfo& method,
    std::size_t bytes, abc::StringBudget strings = {});
}  // namespace abcd::source

#endif  // ABCD_SOURCE_STATIC_FIELDS_H_
