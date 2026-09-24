#ifndef ABCD_ANALYSIS_VALUE_KINDS_H_
#define ABCD_ANALYSIS_VALUE_KINDS_H_

#include <cstddef>
#include <cstdint>

namespace abcd::analysis {
enum class VmValueKind { kLanguage, kHole, kEnvironment, kOtherInternal };
inline constexpr std::size_t kValueKindCount = 4;

// Closed powerset of VM categories with a must-be-string refinement.
// Joining a string with any other nonempty fact drops that refinement.
// Empty is analysis bottom, never JS undefined or a language-value proof.
class ValueKinds {
 public:
  constexpr ValueKinds() = default;
  static constexpr ValueKinds Language() { return ValueKinds{kLanguage}; }
  static constexpr ValueKinds String() { return ValueKinds{kLanguage, true}; }
  static constexpr ValueKinds Hole() { return ValueKinds{kHole}; }
  static constexpr ValueKinds Environment() { return ValueKinds{kEnvironment}; }
  static constexpr ValueKinds OtherInternal() {
    return ValueKinds{kOtherInternal};
  }
  static constexpr ValueKinds Unknown() {
    return Language().Join(Hole()).Join(Environment()).Join(OtherInternal());
  }
  [[nodiscard]] constexpr ValueKinds Join(ValueKinds other) const {
    if (IsEmpty()) return other;
    if (other.IsEmpty()) return *this;
    return ValueKinds{static_cast<std::uint8_t>(bits_ | other.bits_),
                      string_only_ && other.string_only_};
  }
  [[nodiscard]] constexpr bool IsEmpty() const { return bits_ == 0; }
  [[nodiscard]] constexpr bool IsLanguage() const { return bits_ == kLanguage; }
  [[nodiscard]] constexpr bool IsString() const {
    return IsLanguage() && string_only_;
  }
  [[nodiscard]] constexpr bool Contains(VmValueKind kind) const {
    switch (kind) {
      case VmValueKind::kLanguage:
        return (bits_ & kLanguage) != 0;
      case VmValueKind::kHole:
        return (bits_ & kHole) != 0;
      case VmValueKind::kEnvironment:
        return (bits_ & kEnvironment) != 0;
      case VmValueKind::kOtherInternal:
        return (bits_ & kOtherInternal) != 0;
    }
    return false;
  }
  constexpr bool operator==(const ValueKinds&) const = default;

 private:
  static constexpr std::uint8_t kLanguage = 1 << 0;
  static constexpr std::uint8_t kHole = 1 << 1;
  static constexpr std::uint8_t kEnvironment = 1 << 2;
  static constexpr std::uint8_t kOtherInternal = 1 << 3;
  explicit constexpr ValueKinds(std::uint8_t bits, bool string_only = false)
      : bits_(bits), string_only_(string_only) {}
  std::uint8_t bits_ = 0;
  bool string_only_ = false;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_VALUE_KINDS_H_
