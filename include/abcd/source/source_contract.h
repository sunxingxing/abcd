#ifndef ABCD_SOURCE_SOURCE_CONTRACT_H_
#define ABCD_SOURCE_SOURCE_CONTRACT_H_

#include <cstdint>
#include <exception>
#include <string_view>

#include "abcd/analysis/exception_value_flow.h"

namespace abcd::source {
enum class SourceContract : std::uint8_t { kLanguageAndHostV1, kLanguageAndHostV2 };
enum class RealmBindingPolicy : std::uint8_t { kExplicitHost, kStableGlobalThis };
enum class ResourceFailureScope : std::uint8_t { kObservable, kExcluded };
// User-approved source equivalence scope (2026-09-25): legal JS/ArkTS and
// compliant host APIs throw arbitrary JS values, never VM-private markers.
// This is a whole-source-engine premise, not an ABC-derived certificate.
// Keep the version in published artifacts and any future source cache keys.
// V2 also admits normal JS call results, stable unshadowed globalThis, and
// excludes VM resource exhaustion differences. See source-contract-v2.md.
inline constexpr SourceContract kSourceContract = SourceContract::kLanguageAndHostV2;

constexpr analysis::PendingExceptionValues PendingExceptions(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
      return analysis::PendingExceptionValues::kLanguage;
  }
  std::terminate();
}
constexpr analysis::CallResultValues NormalCallResults(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return analysis::CallResultValues::kUnknown;
    case SourceContract::kLanguageAndHostV2: return analysis::CallResultValues::kLanguage;
  }
  std::terminate();
}
constexpr RealmBindingPolicy RealmBinding(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return RealmBindingPolicy::kExplicitHost;
    case SourceContract::kLanguageAndHostV2: return RealmBindingPolicy::kStableGlobalThis;
  }
  std::terminate();
}
constexpr ResourceFailureScope ResourceFailures(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return ResourceFailureScope::kObservable;
    case SourceContract::kLanguageAndHostV2: return ResourceFailureScope::kExcluded;
  }
  std::terminate();
}
// Serialization boundary only. Never dispatch engine semantics on this label.
constexpr std::string_view SourceContractName(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return "language_and_host_v1";
    case SourceContract::kLanguageAndHostV2: return "language_and_host_v2";
  }
  std::terminate();
}
}  // namespace abcd::source
#endif
