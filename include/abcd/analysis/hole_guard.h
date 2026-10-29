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
// A pure accumulator-to-register copy (`sta vN`): no effect, acc unchanged.
inline bool AccumulatorCopy(const ScalarAssignment* copy) {
  const auto* from = copy ? std::get_if<ScalarValue>(&copy->expression) : nullptr;
  return copy && copy->completion == CompletionEffect::kNormal &&
      std::holds_alternative<abc::RegisterId>(copy->destination) && from &&
      std::holds_alternative<Accumulator>(*from);
}
// A static-name Hole guard of the accumulator.
inline bool AccumulatorGuard(const HoleGuard* guard) {
  return guard && std::holds_alternative<Accumulator>(guard->value) &&
      std::holds_alternative<HoleNameSlot>(guard->name);
}
// Whether the load at `load` is checked by a guard that follows it within
// [load, end), separated only by pure accumulator copies. Same pairing as the
// normal and exception module-guard proofs. O(copies).
template <class Copy, class Guard>
bool GuardedAfterCopies(std::size_t load, std::size_t end, Copy copy_at, Guard guard_at) {
  auto at = load + 1;
  while (at < end && copy_at(at)) ++at;
  return at < end && guard_at(at);
}
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_HOLE_GUARD_H_
