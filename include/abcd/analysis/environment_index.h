#ifndef ABCD_ANALYSIS_ENVIRONMENT_INDEX_H_
#define ABCD_ANALYSIS_ENVIRONMENT_INDEX_H_

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <variant>

#include "abcd/analysis/lexical.h"

namespace abcd::analysis {
struct EnvironmentId {
  std::uint32_t value = 0;
  auto operator<=>(const EnvironmentId&) const = default;
};
struct CapturedDepth {
  std::uint32_t value = 0;
  auto operator<=>(const CapturedDepth&) const = default;
};
using EnvironmentRef = std::variant<CapturedDepth, EnvironmentId>;
struct EnvironmentDefinition {
  EnvironmentRef parent;
  PushEnvironment allocation;
};
struct LexicalBinding {
  EnvironmentRef environment;
  LexicalSlot slot;
};
enum class EnvironmentError {
  kInvalidParent,
  kInvalidEnvironment,
  kSlotRange,
  kDepthRange,
  kResourceLimit
};
struct EnvironmentBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// Immutable ancestry index for ONE ordinary lexical capture context. Definition
// IDs are input positions, and local parents must precede children. Captured
// depth zero is the unknown method-entry environment, not a fresh local scope.
// Resolve checks only known local slot bounds, NOT TDZ/value initialization.
// Keeping IDs in context is a caller precondition, not a runtime owner-tag
// check. Distinct allocations never merge by slot/depth.
// The caller must still preserve dynamic freshness in loops, resolve CFG joins,
// connect captures across functions, and keep sendable environments separate.
// O(N * log D) construction/storage, O(log D) query, D = 2^16 ISA depth range.
// Fixed-array payload is admitted before allocation; caller admits definitions,
// allocator overhead and all other work into the whole-process memory limit.
class EnvironmentIndex {
 public:
  EnvironmentIndex(EnvironmentIndex&&) noexcept = default;
  EnvironmentIndex& operator=(EnvironmentIndex&&) noexcept = default;
  EnvironmentIndex(const EnvironmentIndex&) = delete;
  EnvironmentIndex& operator=(const EnvironmentIndex&) = delete;

  [[nodiscard]] static std::variant<EnvironmentIndex, EnvironmentError> Build(
      std::span<const EnvironmentDefinition> definitions,
      EnvironmentBudget budget = {});
  [[nodiscard]] std::variant<EnvironmentRef, EnvironmentError> Ancestor(
      EnvironmentRef current, LexicalDepth distance) const;
  [[nodiscard]] std::variant<LexicalBinding, EnvironmentError> Resolve(
      EnvironmentRef current, LexicalAddress address) const;
  [[nodiscard]] const EnvironmentDefinition* Find(EnvironmentId id) const;
  [[nodiscard]] std::size_t Size() const;
  [[nodiscard]] std::size_t AllocatedBytes() const;

 private:
  EnvironmentIndex() = default;
  static constexpr std::size_t kAncestorLevels =
      std::numeric_limits<decltype(LexicalDepth{}.value)>::digits;
  struct Node {
    EnvironmentDefinition definition;
    std::array<std::optional<EnvironmentId>, kAncestorLevels> ancestors;
    CapturedDepth capture;
    std::uint32_t height = 0;
  };
  std::unique_ptr<Node[]> nodes_;
  std::size_t size_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ENVIRONMENT_INDEX_H_
