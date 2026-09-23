#ifndef ABCD_SOURCE_RECORD_OBJECT_H_
#define ABCD_SOURCE_RECORD_OBJECT_H_

#include <iosfwd>
#include <span>
#include <string>
#include <variant>

#include "abcd/source/emission.h"
#include "abcd/source/static_fields.h"

namespace abcd::source {
struct RecordObjectField {
  std::u16string key;
  StaticAtom value;
};

// The caller proves a fresh unescaped ordinary object and constant values.
// Computed keys preserve DefineField, including an own "__proto__" property.
[[nodiscard]] std::variant<std::size_t, EmitError> WriteRecordObject(
    std::span<const RecordObjectField> fields, std::ostream& output,
    EmitBudget budget);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_OBJECT_H_
