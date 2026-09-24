#ifndef ABCD_ANALYSIS_REGISTER_EFFECT_H_
#define ABCD_ANALYSIS_REGISTER_EFFECT_H_

#include "abcd/analysis/ordinary_call.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
// Pinned DEFINEGETTERSETTERBYVALUE has four register operands plus acc.
inline constexpr std::size_t kAccessorRegisterReads = 4;
inline constexpr std::size_t kFixedRegisterReads = kAccessorRegisterReads + 1;
static_assert(kFixedRegisterReads >= kFixedCallArguments + 2);
struct RegisterEffect {
  std::array<ScalarSlot, kFixedRegisterReads> reads{};
  std::uint8_t read_count = 0;
  std::optional<CallRange> range;
  std::optional<ScalarSlot> write;
};

// Validated decoded input. Reads are conservative may-reads (including Hole
// diagnostics), NOT evaluation order or permission to speculate. Write is a
// definite definition on NORMAL completion only. No heap effects, sentinel
// type proof, exception edges or source-equivalence certification are implied.
// Fixed storage/time; range arguments stay compact until method indexing.
[[nodiscard]] std::variant<RegisterEffect, ScalarError> LiftRegisterEffect(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_REGISTER_EFFECT_H_
