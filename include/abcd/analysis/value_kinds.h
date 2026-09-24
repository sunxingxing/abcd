#ifndef ABCD_ANALYSIS_VALUE_KINDS_H_
#define ABCD_ANALYSIS_VALUE_KINDS_H_

#include <cstddef>
#include <cstdint>

namespace abcd::analysis {
enum class VmValueKind { kLanguage, kHole, kEnvironment, kOtherInternal };
inline constexpr std::size_t kValueKindCount = 4;

// Closed powerset of VM categories. String and non-NaN-number refine Defined
// (a Language value other than undefined); their mixed join retains Defined.
// FiniteNonzeroNumber is a strict numeric subset; joins with other non-NaN
// numbers retain the numeric fact but lose finite/nonzero guarantees.
// Empty is analysis bottom, never JS undefined or a language-value proof.
class ValueKinds {
 public:
  constexpr ValueKinds() = default;
  static constexpr ValueKinds Language() { return ValueKinds{kLanguage}; }
  static constexpr ValueKinds Defined() {
    return ValueKinds{kLanguage, Refinement::kDefined};
  }
  static constexpr ValueKinds String() {
    return ValueKinds{kLanguage, Refinement::kString};
  }
  static constexpr ValueKinds NonNaNNumber() {
    return ValueKinds{kLanguage, Refinement::kNonNaNNumber};
  }
  static constexpr ValueKinds FiniteNonzeroNumber() {
    return ValueKinds{kLanguage, Refinement::kFiniteNonzeroNumber};
  }
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
    const auto refinement = refinement_ == other.refinement_ ? refinement_
        : IsNonNaNNumber() && other.IsNonNaNNumber()
            ? Refinement::kNonNaNNumber
        : IsDefined() && other.IsDefined() ? Refinement::kDefined
                                          : Refinement::kNone;
    return ValueKinds{static_cast<std::uint8_t>(bits_ | other.bits_), refinement};
  }
  [[nodiscard]] constexpr bool IsEmpty() const { return bits_ == 0; }
  [[nodiscard]] constexpr bool IsLanguage() const { return bits_ == kLanguage; }
  // An initialized super guard excludes undefined as well as Hole. This is
  // not an object-identity proof, and Language-only captures cannot provide it.
  [[nodiscard]] constexpr bool IsDefined() const {
    if (!IsLanguage()) return false;
    switch (refinement_) {
      case Refinement::kNone: return false;
      case Refinement::kDefined:
      case Refinement::kString:
      case Refinement::kFiniteNonzeroNumber:
      case Refinement::kNonNaNNumber: return true;
    }
    return false;
  }
  [[nodiscard]] constexpr bool IsString() const {
    return IsLanguage() && refinement_ == Refinement::kString;
  }
  [[nodiscard]] constexpr bool IsNonNaNNumber() const {
    return IsLanguage() && (refinement_ == Refinement::kNonNaNNumber ||
                           refinement_ == Refinement::kFiniteNonzeroNumber);
  }
  [[nodiscard]] constexpr bool IsFiniteNonzeroNumber() const {
    return IsLanguage() && refinement_ == Refinement::kFiniteNonzeroNumber;
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
  enum class Refinement : std::uint8_t {
    kNone, kString, kNonNaNNumber, kDefined, kFiniteNonzeroNumber
  };
  static constexpr std::uint8_t kLanguage = 1 << 0;
  static constexpr std::uint8_t kHole = 1 << 1;
  static constexpr std::uint8_t kEnvironment = 1 << 2;
  static constexpr std::uint8_t kOtherInternal = 1 << 3;
  explicit constexpr ValueKinds(
      std::uint8_t bits, Refinement refinement = Refinement::kNone)
      : bits_(bits), refinement_(refinement) {}
  std::uint8_t bits_ = 0;
  Refinement refinement_ = Refinement::kNone;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_VALUE_KINDS_H_
