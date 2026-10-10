#ifndef ABCD_SOURCE_TRUTH_DOMAINS_H_
#define ABCD_SOURCE_TRUTH_DOMAINS_H_
#include "primitive_value.h"
#include "expression_observation.h"
namespace abcd::source {
// ECMA-262 ToBoolean and IsNullish partition language values without invoking
// user code. Null and undefined stay distinct for absorbed equality tests.
// This is a semantic lattice, never a bound on selections or graph shapes.
enum class TruthDomain : std::uint8_t {kNull,kUndefined,kFalsy,kTruthy,kCount};
class TruthDomains {
 public:
  constexpr TruthDomains() = default;
  static constexpr TruthDomains One(TruthDomain value) {return TruthDomains{static_cast<std::uint8_t>(1U<<static_cast<unsigned>(value))};}
  static constexpr TruthDomains All() {return TruthDomains{static_cast<std::uint8_t>((1U<<static_cast<unsigned>(TruthDomain::kCount))-1)};}
  static constexpr TruthDomains Truthy() {return One(TruthDomain::kTruthy);}
  static constexpr TruthDomains Nullish() {return One(TruthDomain::kNull)|One(TruthDomain::kUndefined);}
  [[nodiscard]] constexpr bool Empty() const {return bits_==0;}
  [[nodiscard]] constexpr TruthDomains operator|(TruthDomains other) const {return TruthDomains{static_cast<std::uint8_t>(bits_|other.bits_)};}
  [[nodiscard]] constexpr TruthDomains operator&(TruthDomains other) const {return TruthDomains{static_cast<std::uint8_t>(bits_&other.bits_)};}
  [[nodiscard]] constexpr TruthDomains operator^(TruthDomains other) const {return TruthDomains{static_cast<std::uint8_t>(bits_^other.bits_)};}
  constexpr TruthDomains& operator|=(TruthDomains other) {return *this=*this|other;}
  constexpr TruthDomains& operator^=(TruthDomains other) {return *this=*this^other;}
  [[nodiscard]] constexpr TruthDomains Complement() const {return *this^All();}
  auto operator<=>(const TruthDomains&) const = default;
 private:
  explicit constexpr TruthDomains(std::uint8_t bits):bits_(bits) {}
  std::uint8_t bits_=0;
};
enum class PureTruthOutcome : std::uint8_t {kInput,kBoolean,kDecision};
// A proved pure function/path over one captured language input. Evaluated
// domains distinguish later short-circuit tests from their shared input
// occurrence; true_values describe Boolean true or the raw taken decision.
// This is not a physical source action or the enclosing selection's value.
struct PureTruthTest {
  TruthDomains evaluated,true_values;
  PureTruthOutcome outcome;
  bool operator==(const PureTruthTest&) const = default;
};
// The grammar adapter and every consumer share short-circuit meaning. This
// returns the left-value domains in which the right operand is evaluated.
constexpr TruthDomains LogicalRightDomains(TreeLogical operation) {
  switch(operation) {
    case TreeLogical::kAnd:return TruthDomains::Truthy();
    case TreeLogical::kOr:return TruthDomains::Truthy().Complement();
    case TreeLogical::kNullish:return TruthDomains::Nullish();
  }
  throw TreeStructureError::kUnsupportedSyntax;
}
inline TruthDomains KindTruthDomains(analysis::ValueKinds kinds) {
  if(kinds.IsEmpty())return {};
  if(kinds.IsFiniteNonzeroNumber())return TruthDomains::Truthy();
  if(kinds.IsNumber()||kinds.IsString()||kinds.IsBoolean())
    return TruthDomains::One(TruthDomain::kFalsy)|TruthDomains::Truthy();
  if(kinds.IsDefined())return TruthDomains::One(TruthDomain::kUndefined).Complement();
  return TruthDomains::All(); // Includes unproved VM-private categories.
}
inline TruthDomains PrimitiveTruthDomains(const TreeLiteral& literal,std::span<const std::u16string> texts) {
  if(std::holds_alternative<NullLiteral>(literal))return TruthDomains::One(TruthDomain::kNull);
  if(std::holds_alternative<UndefinedLiteral>(literal))return TruthDomains::One(TruthDomain::kUndefined);
  return TruthDomains::One(PrimitiveTruth(literal,texts)?TruthDomain::kTruthy:TruthDomain::kFalsy);
}
struct NormalValueDomain {
  analysis::ValueKinds kinds;
  TruthDomains truth;
  NormalValueDomain() = default;
  NormalValueDomain(analysis::ValueKinds value):kinds(value),truth(KindTruthDomains(value)) {}
  NormalValueDomain(analysis::ValueKinds value,TruthDomains domains):kinds(value),truth(domains&KindTruthDomains(value)) {}
  [[nodiscard]] NormalValueDomain Join(NormalValueDomain other) const {return {kinds.Join(other.kinds),truth|other.truth};}
  [[nodiscard]] NormalValueDomain Meet(analysis::ValueKinds other) const {return {kinds.Meet(other),truth};}
  bool operator==(const NormalValueDomain&) const = default;
};
}
#endif
