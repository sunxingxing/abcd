#ifndef ABCD_ANALYSIS_ARITHMETIC_SEMANTICS_H_
#define ABCD_ANALYSIS_ARITHMETIC_SEMANTICS_H_

#include <cstdint>

namespace abcd::analysis {
// Explicit source equivalence premise, never inferred from a sample or a
// numeric value. Source-language binary arithmetic follows ECMAScript;
// pinned-runtime arithmetic retains the representation proofs. Neither mode
// grants language identity to input markers or changes effects/exceptions.
enum class ArithmeticSemantics : std::uint8_t { kPinnedRuntime, kSourceLanguage };
}  // namespace abcd::analysis
#endif
