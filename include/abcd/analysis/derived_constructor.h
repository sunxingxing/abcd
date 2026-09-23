#ifndef ABCD_ANALYSIS_DERIVED_CONSTRUCTOR_H_
#define ABCD_ANALYSIS_DERIVED_CONSTRUCTOR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"

namespace abcd::analysis {
struct SuperForward {
  abc::RegisterId constructor;
};
struct SuperRange {
  abc::RegisterId first;
  std::uint16_t count;
  // Narrow form's IC metadata is retained but never printed as an argument.
  std::optional<std::uint16_t> cache_slot;
};
enum class SuperCheckKind {
  kRequireInitialized,
  kRequireUninitialized,
  kNoop
};
struct SuperCheck {
  SuperCheckKind kind;
};
enum class SuperError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using SuperInstruction =
    std::variant<SuperForward, SuperRange, SuperCheck, SuperError>;
[[nodiscard]] SuperInstruction LiftSuperInstruction(
    const abc::DecodedInstruction& instruction, std::uint32_t registers);

enum class ForwardingKind { kAllArguments, kParameters };
struct ForwardingDerivedConstructor {
  ForwardingKind kind = ForwardingKind::kAllArguments;
  std::uint16_t argument_count = 0;
};
enum class DerivedConstructorError {
  kFrame,
  kControlFlow,
  kInstruction,
  kValue,
  kGuard,
  kReturn,
  kResourceLimit
};
struct DerivedConstructorBudget {
  static constexpr std::size_t kDefaultBytes = 16 * 1024 * 1024;
  std::size_t bytes = kDefaultBytes;
};
using DerivedConstructorResult = std::variant<ForwardingDerivedConstructor,
                                              DerivedConstructorError>;
// Pinned v7 constructor-template proof, O(instructions + frame registers).
// The caller must separately prove derived class creation, parent identity,
// method target/ABI and observable length before emitting JS `super(...)` or
// an explicit ordered parameter list. kParameters proves each range value is
// the corresponding ABI parameter, even after pure local copies.
// No side-effecting operation or write to an ABI input slot is admitted.
[[nodiscard]] DerivedConstructorResult ProveForwardingDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget = {});
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_DERIVED_CONSTRUCTOR_H_
