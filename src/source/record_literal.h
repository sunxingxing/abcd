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

[[nodiscard]] std::variant<std::size_t, EmitError> WriteDenseArray(
    std::span<const StaticAtom> elements, std::ostream& output, EmitBudget budget);

// Caller proves a fresh, unescaped, single-parent tree. Object data keys keep
// "__proto__" computed to reproduce its own DefineField property; dense array
// elements reproduce STOWNBYINDEX. Depth is bounded independently of bytes.
[[nodiscard]] RecordLiteralResult WriteRecordLiteral(
    std::span<const RecordLiteralNode> nodes, std::size_t root,
    std::ostream& output, EmitBudget budget,
    std::span<const std::string> imports = {});
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_LITERAL_H_
