#ifndef ABCD_ANALYSIS_REGEXP_H_
#define ABCD_ANALYSIS_REGEXP_H_
#include "abcd/analysis/regexp_flags.h"
#include "abcd/analysis/scalar.h"
namespace abcd::analysis {
struct RegExpLiteral {
  abc::PoolIndex pattern;
  RegExpFlags flags;
};
std::variant<RegExpLiteral, ScalarError> LiftRegExpLiteral(
    const abc::DecodedInstruction&);
}  // namespace abcd::analysis
#endif
