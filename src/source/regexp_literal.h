#ifndef ABCD_SOURCE_REGEXP_LITERAL_H_
#define ABCD_SOURCE_REGEXP_LITERAL_H_
#include <string>

#include "abcd/analysis/regexp_flags.h"
#include "abcd/analysis/semantic_ir.h"
namespace abcd::source {
enum class RegExpLiteralError : std::uint8_t {
  kFlags,
  kSyntax,
  kUnproven,
  kSourceRepresentation,
  kResourceLimit,
  kWorkLimit
};
struct RegExpLiteralPlan {
  std::string literal;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Conservative ECMA Pattern grammar validation; never execute a regexp or
// turn an unproved runtime SyntaxError into a module's early SyntaxError.
std::variant<RegExpLiteralPlan, RegExpLiteralError> BuildRegExpLiteral(
    std::u16string_view, analysis::RegExpFlags, analysis::SemanticBudget = {});
bool VerifyRegExpLiteral(std::u16string_view, analysis::RegExpFlags,
                         const RegExpLiteralPlan&,
                         analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
