#ifndef ABCD_SOURCE_STRING_LITERAL_H_
#define ABCD_SOURCE_STRING_LITERAL_H_

#include <cstddef>
#include <iosfwd>
#include <string_view>
#include <variant>

#include "abcd/source/emission.h"

namespace abcd::source {

// Emits one double-quoted, ASCII-only JS string expression, preserving UTF-16
// units exactly. This is JS source escaping, not HTML/script-element escaping.
// O(input units), fixed scratch storage. Limit failure writes nothing; an IO
// failure may leave partial output. Caller owns input and output storage.
[[nodiscard]] std::variant<std::size_t, EmitError> WriteStringLiteral(
    std::u16string_view value, std::ostream& output, EmitBudget budget = {});

}  // namespace abcd::source

#endif  // ABCD_SOURCE_STRING_LITERAL_H_
