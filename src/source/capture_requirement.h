#ifndef ABCD_SOURCE_CAPTURE_REQUIREMENT_H_
#define ABCD_SOURCE_CAPTURE_REQUIREMENT_H_

#include "abcd/analysis/creation_binding.h"

namespace abcd::source {
// Join requirements for ONE cell, not access proofs for individual instructions.
// Guarded < SourceAccess < Language. Defined carries an additional strict
// obligation: keep it intact, never discharge it using a Language/TDZ premise.
// An owner which cannot represent a joined Defined obligation must refuse.
[[nodiscard]] constexpr analysis::CaptureDomain JoinCaptureRequirement(
    analysis::CaptureDomain left, analysis::CaptureDomain right) {
  using Domain = analysis::CaptureDomain;
  switch (left) {
    case Domain::kGuarded: return right;
    case Domain::kSourceAccess: return right == Domain::kGuarded ? left : right;
    case Domain::kLanguage: return right == Domain::kDefined ? right : left;
    case Domain::kDefined: return left;
  }
  return Domain::kDefined;
}
// Changing an original access's family obligation may only strengthen the
// three TDZ/Language requirements. Defined is independent and stays unchanged.
[[nodiscard]] constexpr bool StrengthensCaptureRequirement(
    analysis::CaptureDomain original, analysis::CaptureDomain joined) {
  using Domain = analysis::CaptureDomain;
  switch (original) {
    case Domain::kGuarded:
      return joined == Domain::kGuarded || joined == Domain::kSourceAccess || joined == Domain::kLanguage;
    case Domain::kSourceAccess:
      return joined == Domain::kSourceAccess || joined == Domain::kLanguage;
    case Domain::kLanguage: return joined == Domain::kLanguage;
    case Domain::kDefined: return joined == Domain::kDefined;
  }
  return false;
}
}  // namespace abcd::source
#endif
