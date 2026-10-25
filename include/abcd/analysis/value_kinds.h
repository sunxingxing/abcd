#ifndef ABCD_ANALYSIS_VALUE_KINDS_H_
#define ABCD_ANALYSIS_VALUE_KINDS_H_

#include <cstddef>
#include <cstdint>

namespace abcd::analysis {
enum class VmValueKind { kLanguage, kHole, kEnvironment, kOtherInternal, kPropertyEnumerator, kPatternRecord };
inline constexpr std::size_t kValueKindCount = 6;

// Closed powerset of VM categories. String and non-NaN-number refine Defined
// (a Language value other than undefined); their mixed join retains Defined.
// FiniteNonzeroNumber refines FiniteNumber, which includes both signed zeros.
// Joins weaken finite and nonzero information independently of the separately
// tracked exclusion of the VM tagged INT32_MIN representation.
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
  // Exact Boolean survives common SSA copies and all-Boolean joins. It is a
  // subset of ZeroOrOnePrimitive; null must never acquire Boolean semantics.
  static constexpr ValueKinds Boolean() {
    return ValueKinds{kLanguage, Refinement::kBoolean};
  }
  [[nodiscard]] constexpr bool IsBoolean() const {
    return IsLanguage() && refinement_ == Refinement::kBoolean;
  }
  // Boolean or null: neither is Number, but ToNumber/ToNumeric returns
  // exactly zero or one without invoking user code. Undefined is excluded.
  static constexpr ValueKinds ZeroOrOnePrimitive() {
    return ValueKinds{kLanguage, Refinement::kZeroOrOnePrimitive};
  }
  [[nodiscard]] constexpr bool IsZeroOrOnePrimitive() const {
    return IsBoolean() || (IsLanguage() && refinement_ == Refinement::kZeroOrOnePrimitive);
  }
  static constexpr ValueKinds Number() { return ValueKinds{kLanguage, Refinement::kNumber}; }
  static constexpr ValueKinds NonNaNNumber() {
    return ValueKinds{kLanguage, Refinement::kNonNaNNumber};
  }
  static constexpr ValueKinds FiniteNumber() {
    return ValueKinds{kLanguage, Refinement::kFiniteNumber};
  }
  static constexpr ValueKinds FiniteNonzeroNumber() {
    return ValueKinds{kLanguage, Refinement::kFiniteNonzeroNumber};
  }
  static constexpr ValueKinds Hole() { return ValueKinds{kHole}; }
  static constexpr ValueKinds Environment() { return ValueKinds{kEnvironment}; }
  static constexpr ValueKinds OtherInternal() {
    return ValueKinds{kOtherInternal};
  }
  static constexpr ValueKinds PropertyEnumerator() { return ValueKinds{kPropertyEnumerator}; }
  // Internal array-pattern record (acquired iterator, cached next, done).
  static constexpr ValueKinds PatternRecord() { return ValueKinds{kPatternRecord}; }
  static constexpr ValueKinds Unknown() {
    return Language().Join(Hole()).Join(Environment()).Join(OtherInternal()).Join(PropertyEnumerator()).Join(PatternRecord());
  }
  [[nodiscard]] constexpr ValueKinds Join(ValueKinds other) const {
    if (IsEmpty()) return other;
    if (other.IsEmpty()) return *this;
    const auto refinement = refinement_ == other.refinement_ ? refinement_
        : IsFiniteNumber() && other.IsFiniteNumber() ? Refinement::kFiniteNumber
        : IsNonNaNNumber() && other.IsNonNaNNumber()
            ? Refinement::kNonNaNNumber
        : IsNumber() && other.IsNumber() ? Refinement::kNumber
        : IsZeroOrOnePrimitive() && other.IsZeroOrOnePrimitive() ? Refinement::kZeroOrOnePrimitive
        : IsDefined() && other.IsDefined() ? Refinement::kDefined
                                          : Refinement::kNone;
    return ValueKinds{static_cast<std::uint8_t>(bits_ | other.bits_), refinement,
                      excludes_min_int32_ && other.excludes_min_int32_,
                      integral_number_ && other.integral_number_};
  }
  // Intersect two independent proofs. Refinements form a tree within the
  // Language category; representation/integrality are independent facts.
  // Unlike a conditional narrowing after Join, this is monotone even when
  // one operand is a partially solved domain. Bottom remains bottom.
  [[nodiscard]] constexpr ValueKinds Meet(ValueKinds other) const {
    auto bits = static_cast<std::uint8_t>(bits_ & other.bits_);
    if (!(bits & kLanguage)) return ValueKinds{bits};
    const ValueKinds left{kLanguage, refinement_}, right{kLanguage, other.refinement_};
    Refinement refinement;
    if (left.Join(right) == right) refinement = refinement_;
    else if (left.Join(right) == left) refinement = other.refinement_;
    else return ValueKinds{static_cast<std::uint8_t>(bits & ~kLanguage)};
    auto result = ValueKinds{bits, refinement};
    if (result.IsNonNaNNumber()) result.excludes_min_int32_ = excludes_min_int32_ || other.excludes_min_int32_;
    if (result.IsFiniteNumber()) result.integral_number_ = integral_number_ || other.integral_number_;
    return result;
  }
  // Edge-local exclusion never promotes other internal categories to Language.
  [[nodiscard]] constexpr ValueKinds WithoutHole() const {
    auto result = *this;
    result.bits_ &= static_cast<std::uint8_t>(~kHole);
    return result;
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
      case Refinement::kBoolean:
      case Refinement::kZeroOrOnePrimitive:
      case Refinement::kNumber:
      case Refinement::kFiniteNonzeroNumber:
      case Refinement::kFiniteNumber:
      case Refinement::kNonNaNNumber: return true;
    }
    return false;
  }
  [[nodiscard]] constexpr bool IsString() const {
    return IsLanguage() && refinement_ == Refinement::kString;
  }
  // Number includes NaN but excludes BigInt and every VM-private marker.
  [[nodiscard]] constexpr bool IsNumber() const {
    return IsLanguage() && (refinement_ == Refinement::kNumber || IsNonNaNNumber());
  }
  [[nodiscard]] constexpr bool IsNonNaNNumber() const {
    return IsLanguage() && (refinement_ == Refinement::kNonNaNNumber ||
                           refinement_ == Refinement::kFiniteNumber ||
                           refinement_ == Refinement::kFiniteNonzeroNumber);
  }
  [[nodiscard]] constexpr bool IsFiniteNumber() const {
    return IsLanguage() && (refinement_ == Refinement::kFiniteNumber ||
                           refinement_ == Refinement::kFiniteNonzeroNumber);
  }
  [[nodiscard]] constexpr bool IsFiniteNonzeroNumber() const {
    return IsLanguage() && refinement_ == Refinement::kFiniteNonzeroNumber;
  }
  // Finite integer Number (including signed zero), independent of the VM tag.
  // Joins retain integrality only when every alternative supplies it.
  [[nodiscard]] constexpr ValueKinds WithIntegralNumber() const {
    auto result = *this;
    if (IsFiniteNumber()) result.integral_number_ = true;
    return result;
  }
  [[nodiscard]] constexpr bool IsIntegralNumber() const {
    return IsFiniteNumber() && integral_number_;
  }
  // A VM representation fact, not a numeric range guessed from source names.
  // It survives a join only when both alternatives exclude the tagged minimum.
  [[nodiscard]] constexpr ValueKinds WithoutInt32MinTag() const {
    auto result = *this;
    if (IsNonNaNNumber()) result.excludes_min_int32_ = true;
    return result;
  }
  [[nodiscard]] constexpr bool ExcludesInt32MinTag() const {
    return IsNonNaNNumber() && excludes_min_int32_;
  }
  [[nodiscard]] constexpr bool Contains(VmValueKind kind) const {
    switch (kind) {
      case VmValueKind::kLanguage:
        return (bits_ & kLanguage) != 0;
      case VmValueKind::kHole:
        return (bits_ & kHole) != 0;
      case VmValueKind::kEnvironment:
        return (bits_ & kEnvironment) != 0;
      case VmValueKind::kPropertyEnumerator:
        return (bits_ & kPropertyEnumerator) != 0;
      case VmValueKind::kOtherInternal:
        return (bits_ & kOtherInternal) != 0;
      case VmValueKind::kPatternRecord:
        return (bits_ & kPatternRecord) != 0;
    }
    return false;
  }
  constexpr bool operator==(const ValueKinds&) const = default;

 private:
  enum class Refinement : std::uint8_t {
    kNone, kString, kNonNaNNumber, kDefined, kFiniteNonzeroNumber, kFiniteNumber, kNumber, kZeroOrOnePrimitive, kBoolean
  };
  static constexpr std::uint8_t kLanguage = 1 << 0;
  static constexpr std::uint8_t kHole = 1 << 1;
  static constexpr std::uint8_t kEnvironment = 1 << 2;
  static constexpr std::uint8_t kOtherInternal = 1 << 3;
  static constexpr std::uint8_t kPropertyEnumerator = 1 << 4;
  static constexpr std::uint8_t kPatternRecord = 1 << 5;
  explicit constexpr ValueKinds(
      std::uint8_t bits, Refinement refinement = Refinement::kNone, bool excludes_min_int32 = false, bool integral_number = false)
      : bits_(bits), refinement_(refinement), excludes_min_int32_(excludes_min_int32),
        integral_number_(integral_number) {}
  std::uint8_t bits_ = 0;
  Refinement refinement_ = Refinement::kNone;
  bool excludes_min_int32_ = false;
  bool integral_number_ = false;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_VALUE_KINDS_H_
