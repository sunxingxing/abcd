#ifndef ABCD_ANALYSIS_ARRAY_CREATION_H_
#define ABCD_ANALYSIS_ARRAY_CREATION_H_

#include <cstdint>
#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::analysis {
struct BufferedArrayCreation {
  abc::PoolIndex literal;
  std::uint16_t cache_slot;
};
enum class ArrayCreationError { kUnsupportedOpcode, kMalformedInstruction };
// OpenHarmony-v7.0-Release isa.yaml createarraywithbuffer: modern regional
// literal slot, with narrow/wide IC metadata kept separately. Deprecated
// global-index encoding needs its own profile and must not be reinterpreted.
[[nodiscard]] std::variant<BufferedArrayCreation, ArrayCreationError>
LiftArrayCreation(const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ARRAY_CREATION_H_
