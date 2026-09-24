#ifndef ABCD_SOURCE_RECORD_LITERAL_H_
#define ABCD_SOURCE_RECORD_LITERAL_H_

#include <iosfwd>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "abcd/source/emission.h"
#include "abcd/source/literal_graph.h"

namespace abcd::source {
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
