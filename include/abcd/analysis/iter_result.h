#ifndef ABCD_ANALYSIS_ITER_RESULT_H_
#define ABCD_ANALYSIS_ITER_RESULT_H_

#include <cstdint>
#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct IterResult {
  abc::RegisterId value;
  abc::RegisterId done;
};
enum class IterResultError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// v7 CREATEITERRESULTOBJ reads value and boolean done from two registers,
// then replaces acc with a fresh ordinary object. The runtime's IsTrue test
// is exact true, not JS truthiness. Source admission is profile-gated later.
[[nodiscard]] std::variant<IterResult, IterResultError> LiftIterResult(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ITER_RESULT_H_
