#ifndef ABCD_ANALYSIS_ARRAY_SPREAD_H_
#define ABCD_ANALYSIS_ARRAY_SPREAD_H_

#include "abcd/abc/register_access.h"

namespace abcd::analysis {
// runtime_core 3e147bb7 isa.yaml STARRAYSPREAD; ets_runtime f04900cf
// RuntimeStArraySpread. Acc is the iterable input, then the next index on
// success. Destination may already be partly mutated when the operation throws.
struct ArraySpread {
  abc::RegisterId destination;
  abc::RegisterId index;
};
enum class ArraySpreadError : std::uint8_t {
  kUnsupportedOpcode, kMalformedInstruction, kRegisterRange
};
[[nodiscard]] std::variant<ArraySpread, ArraySpreadError> LiftArraySpread(
    const abc::DecodedInstruction&, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif
