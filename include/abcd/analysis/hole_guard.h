#ifndef ABCD_ANALYSIS_HOLE_GUARD_H_
#define ABCD_ANALYSIS_HOLE_GUARD_H_

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
struct LoadHole {};
struct HoleNameSlot {
  abc::PoolIndex slot;
};
struct HoleGuard {
  ScalarSlot value;
  std::variant<abc::RegisterId, HoleNameSlot> name;
};
enum class HoleError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using HoleResult = std::variant<LoadHole, HoleGuard, HoleError>;

// LoadHole writes the VM sentinel to acc, never a JS undefined value. Guard
// tests only identity with Hole: non-Hole returns normally without reading the
// name; Hole reads the name then raises the runtime's reference error. Guards
// do not assign registers/acc. Keep name evaluation conditional, including
// string-pool lookup. Binding/TDZ reconstruction and catch routing are
// separate. Requires DecodeInstruction output; fixed time/storage, no runtime
// evaluation.
[[nodiscard]] HoleResult LiftHoleInstruction(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_HOLE_GUARD_H_
