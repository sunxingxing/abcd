#ifndef ABCD_ANALYSIS_ARITHMETIC_SEMANTICS_H_
#define ABCD_ANALYSIS_ARITHMETIC_SEMANTICS_H_

#include <cstdint>

namespace abcd::analysis {
// Explicit source equivalence premise, never inferred from a sample or a
// numeric value. Source-language binary arithmetic follows ECMAScript;
// SourceNumeric additionally covers unary numeric operations. Pinned-runtime
// arithmetic retains the representation proofs. No mode
// grants language identity to input markers or changes effects/exceptions.
enum class ArithmeticSemantics : std::uint8_t { kPinnedRuntime, kSourceLanguage, kSourceNumeric };
constexpr bool LanguageBinaryArithmetic(ArithmeticSemantics semantics) {
  return semantics == ArithmeticSemantics::kSourceLanguage ||
         semantics == ArithmeticSemantics::kSourceNumeric;
}
}  // namespace abcd::analysis
#endif
