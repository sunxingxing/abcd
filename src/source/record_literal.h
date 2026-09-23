#ifndef ABCD_SOURCE_RECORD_LITERAL_H_
#define ABCD_SOURCE_RECORD_LITERAL_H_

#include <iosfwd>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "abcd/source/emission.h"
#include "abcd/source/static_fields.h"

namespace abcd::source {
struct RecordNodeRef {
  std::size_t id = 0;
};
using RecordLiteralValue = std::variant<StaticAtom, RecordNodeRef>;
struct RecordLiteralField {
  std::u16string key;
  RecordLiteralValue value;
};
enum class RecordLiteralKind { kObject, kArray };
struct RecordLiteralNode {
  RecordLiteralKind kind;
  std::vector<RecordLiteralField> fields;
  std::vector<RecordLiteralValue> elements;
  bool attached = false;
  bool emitted = false;
};
enum class RecordLiteralError { kInvalidGraph, kDepthLimit };
using RecordLiteralResult =
    std::variant<std::size_t, EmitError, RecordLiteralError>;

// Caller proves a fresh, unescaped, single-parent tree. Computed object keys
// reproduce DefineField including an own "__proto__" property; dense array
// elements reproduce STOWNBYINDEX. Depth is bounded independently of bytes.
[[nodiscard]] RecordLiteralResult WriteRecordLiteral(
    std::span<const RecordLiteralNode> nodes, std::size_t root,
    std::ostream& output, EmitBudget budget);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_LITERAL_H_
