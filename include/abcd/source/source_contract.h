#ifndef ABCD_SOURCE_SOURCE_CONTRACT_H_
#define ABCD_SOURCE_SOURCE_CONTRACT_H_

#include <cstdint>
#include <exception>
#include <string_view>

#include "abcd/analysis/exception_value_flow.h"

namespace abcd::source {
enum class SourceContract : std::uint8_t {
  kLanguageAndHostV1, kLanguageAndHostV2, kLanguageAndHostV3
};
enum class RealmBindingPolicy : std::uint8_t { kExplicitHost, kStableGlobalThis };
enum class ResourceFailureScope : std::uint8_t { kObservable, kExcluded };
enum class CallableTextPolicy : std::uint8_t { kUnspecified, kRecovered };
// User-approved source equivalence scope (2026-09-25): legal JS/ArkTS and
// compliant host APIs throw arbitrary JS values, never VM-private markers.
// This is a whole-source-engine premise, not an ABC-derived certificate.
// Keep the version in published artifacts and any future source cache keys.
// V2 also admits normal JS call results, stable unshadowed globalThis, and
// excludes VM resource exhaustion differences. See source-contract-v2.md.
// V3 additionally permits recovered callable source text to differ. Runtime
// behavior, scopes, parameters, results and effects still require proof.
// User approval: 2026-09-27; see source-contract-v3.md. Never reinterpret V2.
inline constexpr SourceContract kSourceContract = SourceContract::kLanguageAndHostV3;

constexpr analysis::PendingExceptionValues PendingExceptions(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
      return analysis::PendingExceptionValues::kLanguage;
  }
  std::terminate();
}
constexpr analysis::CallResultValues NormalCallResults(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return analysis::CallResultValues::kUnknown;
    case SourceContract::kLanguageAndHostV2: return analysis::CallResultValues::kLanguage;
    case SourceContract::kLanguageAndHostV3: return analysis::CallResultValues::kLanguage;
  }
  std::terminate();
}
constexpr RealmBindingPolicy RealmBinding(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return RealmBindingPolicy::kExplicitHost;
    case SourceContract::kLanguageAndHostV2: return RealmBindingPolicy::kStableGlobalThis;
    case SourceContract::kLanguageAndHostV3: return RealmBindingPolicy::kStableGlobalThis;
  }
  std::terminate();
}
constexpr ResourceFailureScope ResourceFailures(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return ResourceFailureScope::kObservable;
    case SourceContract::kLanguageAndHostV2: return ResourceFailureScope::kExcluded;
    case SourceContract::kLanguageAndHostV3: return ResourceFailureScope::kExcluded;
  }
  std::terminate();
}
constexpr CallableTextPolicy CallableText(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
      return CallableTextPolicy::kUnspecified;
    case SourceContract::kLanguageAndHostV3:
      return CallableTextPolicy::kRecovered;
  }
  std::terminate();
}
// Serialization boundary only. Never dispatch engine semantics on this label.
constexpr std::string_view SourceContractName(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return "language_and_host_v1";
    case SourceContract::kLanguageAndHostV2: return "language_and_host_v2";
    case SourceContract::kLanguageAndHostV3: return "language_and_host_v3";
  }
  std::terminate();
}
}  // namespace abcd::source
#endif
