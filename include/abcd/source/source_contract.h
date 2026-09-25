#ifndef ABCD_SOURCE_SOURCE_CONTRACT_H_
#define ABCD_SOURCE_SOURCE_CONTRACT_H_

#include <cstdint>
#include <exception>
#include <string_view>

#include "abcd/analysis/exception_value_flow.h"

namespace abcd::source {
enum class SourceContract : std::uint8_t { kLanguageAndHostV1 };
// User-approved source equivalence scope (2026-09-25): legal JS/ArkTS and
// compliant host APIs throw arbitrary JS values, never VM-private markers.
// This is a whole-source-engine premise, not an ABC-derived certificate.
// Keep the version in published artifacts and any future source cache keys.
inline constexpr SourceContract kSourceContract = SourceContract::kLanguageAndHostV1;

constexpr analysis::PendingExceptionValues PendingExceptions(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
      return analysis::PendingExceptionValues::kLanguage;
  }
  std::terminate();
}
// Serialization boundary only. Never dispatch engine semantics on this label.
constexpr std::string_view SourceContractName(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return "language_and_host_v1";
  }
  std::terminate();
}
}  // namespace abcd::source
#endif
