#ifndef ABCD_SOURCE_SOURCE_CONTRACT_H_
#define ABCD_SOURCE_SOURCE_CONTRACT_H_

#include <array>
#include <cstdint>
#include <optional>
#include <exception>
#include <string_view>

#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/catch_payload_domain.h"

namespace abcd::source {
// Published IDs 0..7 are retired, not available for reuse or reinterpretation.
enum class SourceContract : std::uint8_t {
  kLanguageSourceV1 = 8, kLanguageSourceV2 = 9, kLanguageSourceV3 = 10,
  kLanguageSourceV4 = 11, kLanguageSourceV5 = 12, kLanguageSourceV6 = 13,
  kLanguageSourceV7 = 14, kLanguageSourceV8 = 15, kLanguageSourceV9 = 16,
  kLanguageSourceV10 = 17
};
// Provenance of an actual binding, independent of the source target contract.
enum class RealmBindingOrigin : std::uint8_t { kExplicitHost, kStableGlobalThis };
// Resource observability remains an independent proof input.
enum class ResourceFailureScope : std::uint8_t { kObservable, kExcluded };

struct SourceContractEntry {
  SourceContract contract;
  std::string_view label;
};
// One boundary registry for writing, parsing and identity admission. Historical
// Source identities keep their published IDs/labels; retired receipts refuse.
inline constexpr std::array kSourceContracts{
    SourceContractEntry{SourceContract::kLanguageSourceV1, "language_source_v1"},
    SourceContractEntry{SourceContract::kLanguageSourceV2, "language_source_v2"},
    SourceContractEntry{SourceContract::kLanguageSourceV3, "language_source_v3"},
    SourceContractEntry{SourceContract::kLanguageSourceV4, "language_source_v4"},
    SourceContractEntry{SourceContract::kLanguageSourceV5, "language_source_v5"},
    SourceContractEntry{SourceContract::kLanguageSourceV6, "language_source_v6"},
    SourceContractEntry{SourceContract::kLanguageSourceV7, "language_source_v7"},
    SourceContractEntry{SourceContract::kLanguageSourceV8, "language_source_v8"},
    SourceContractEntry{SourceContract::kLanguageSourceV9, "language_source_v9"},
    SourceContractEntry{SourceContract::kLanguageSourceV10, "language_source_v10"},
 };
constexpr std::string_view SourceContractName(SourceContract contract) {
  for (const auto& entry : kSourceContracts)
    if (entry.contract == contract) return entry.label;
  return {};
}
constexpr bool IsSourceContract(SourceContract contract) {
  return !SourceContractName(contract).empty();
}
constexpr std::optional<SourceContract> ParseSourceContract(std::string_view label) {
  for (const auto& entry : kSourceContracts)
    if (entry.label == label) return entry.contract;
  return {};
}
// User-approved source equivalence scope (2026-09-25): legal JS/ArkTS and
// compliant host APIs throw arbitrary JS values, never VM-private markers.
// This is a whole-source-engine premise, not an ABC-derived certificate.
// Keep the version in published artifacts and any future source cache keys.
// V2 also admits normal JS call results, stable unshadowed globalThis, and
// excludes VM resource exhaustion differences. See source-contract-v2.md.
// V3 additionally permits recovered callable source text to differ. Runtime
// behavior, scopes, parameters, results and effects still require proof.
// User approval: 2026-09-27; see source-contract-v3.md. Never reinterpret V2.
// The sole CLI target (2026-10-26). Historical identities keep their meanings;
// selecting this default adds no premise beyond the already approved V10.
inline constexpr SourceContract kSourceContract = SourceContract::kLanguageSourceV10;

// Shared source premises. These are not analysis defaults: raw analysis still
// accepts pinned arithmetic/call-result/pending-exception modes independently.
inline constexpr auto kSourceArithmetic = analysis::ArithmeticSemantics::kSourceNumeric;
inline constexpr auto kSourceCallResults = analysis::CallResultValues::kLanguage;
inline constexpr auto kSourcePendingExceptions = analysis::PendingExceptionValues::kLanguage;
inline constexpr auto kSourceResourceFailures = ResourceFailureScope::kExcluded;

// Approved 2026-10-14. V2 adds only standard iterator acquisition; V1 keeps
// its historical four premises. This does not authorize apply/spread calls,
// next re-reads, different close completion priority, or binding changes.
enum class IterationAcquisitionSemantics : std::uint8_t { kPinnedRuntime, kStandardV1 };
constexpr IterationAcquisitionSemantics IterationAcquisition(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageSourceV1: return IterationAcquisitionSemantics::kPinnedRuntime;
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
    case SourceContract::kLanguageSourceV10:
    case SourceContract::kLanguageSourceV9:
    case SourceContract::kLanguageSourceV8:
    case SourceContract::kLanguageSourceV7:
    case SourceContract::kLanguageSourceV6:
    case SourceContract::kLanguageSourceV5: return IterationAcquisitionSemantics::kStandardV1;
  }
  std::terminate();
}
// Approved 2026-10-15: Ark output policies preserve kind and metadata, never
// erase concurrent/sendable/shared semantics. Each policy has independent
// admission; publication requires completion and recompilation proof.
enum class ConcurrentOutputPolicy : std::uint8_t { kNotEnabled, kArkDirectiveV1 };
constexpr ConcurrentOutputPolicy ConcurrentOutput(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
      return ConcurrentOutputPolicy::kNotEnabled;
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
    case SourceContract::kLanguageSourceV10:
    case SourceContract::kLanguageSourceV9:
    case SourceContract::kLanguageSourceV8:
    case SourceContract::kLanguageSourceV7:
    case SourceContract::kLanguageSourceV6:
    case SourceContract::kLanguageSourceV5: return ConcurrentOutputPolicy::kArkDirectiveV1;
  }
  std::terminate();
}
enum class SendableOutputPolicy : std::uint8_t { kNotEnabled, kArkDirectiveV1 };
constexpr SendableOutputPolicy SendableOutput(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
      return SendableOutputPolicy::kNotEnabled;
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
    case SourceContract::kLanguageSourceV10:
    case SourceContract::kLanguageSourceV9:
    case SourceContract::kLanguageSourceV8:
    case SourceContract::kLanguageSourceV7:
    case SourceContract::kLanguageSourceV6:
    case SourceContract::kLanguageSourceV5: return SendableOutputPolicy::kArkDirectiveV1;
  }
  std::terminate();
}
enum class SharedModuleOutputPolicy : std::uint8_t { kNotEnabled, kArkDirectiveV1 };
constexpr SharedModuleOutputPolicy SharedModuleOutput(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
      return SharedModuleOutputPolicy::kNotEnabled;
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
    case SourceContract::kLanguageSourceV10:
    case SourceContract::kLanguageSourceV9:
    case SourceContract::kLanguageSourceV8:
    case SourceContract::kLanguageSourceV7:
    case SourceContract::kLanguageSourceV6:
    case SourceContract::kLanguageSourceV5: return SharedModuleOutputPolicy::kArkDirectiveV1;
  }
  std::terminate();
}

// Approved 2026-10-16: only externally sourced exception payloads exclude
// Hole. Local explicit throws retain their SSA domain, including raw Hole.
constexpr analysis::ExternalExceptionPolicy ExternalExceptionNoHole(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
      return analysis::ExternalExceptionPolicy::kPinnedRuntime;
    case SourceContract::kLanguageSourceV4:
    case SourceContract::kLanguageSourceV10:
    case SourceContract::kLanguageSourceV9:
    case SourceContract::kLanguageSourceV8:
    case SourceContract::kLanguageSourceV7:
    case SourceContract::kLanguageSourceV6:
    case SourceContract::kLanguageSourceV5: return analysis::ExternalExceptionPolicy::kNoHoleV1;
  }
  std::terminate();
}

// Approved 2026-10-19. Only a proved private argument construction consumed
// by one APPLY may omit the hidden array's iterator acquisition in standard JS.
// Callee/receiver, inner spread and all construction events still need proof.
enum class CallSpreadSemantics : std::uint8_t { kPinnedRuntime, kPrivateArgumentsV1 };
constexpr CallSpreadSemantics CallSpread(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return CallSpreadSemantics::kPinnedRuntime;
    case SourceContract::kLanguageSourceV10:
    case SourceContract::kLanguageSourceV9:
    case SourceContract::kLanguageSourceV8:
    case SourceContract::kLanguageSourceV7:
    case SourceContract::kLanguageSourceV6:
    case SourceContract::kLanguageSourceV5: return CallSpreadSemantics::kPrivateArgumentsV1;
  }
  std::terminate();
}

// User approval 2026-10-21: own-frame entry snapshots may expose native
// strict arguments, including escape. Ark's additional own throwing caller
// accessor is the accepted difference; this does not authorize later snapshots.
constexpr bool NativeArguments(SourceContract contract) {
  return contract == SourceContract::kLanguageSourceV6 || (contract == SourceContract::kLanguageSourceV7 || contract == SourceContract::kLanguageSourceV8 || contract == SourceContract::kLanguageSourceV9 || contract == SourceContract::kLanguageSourceV10);
}

// User approval 2026-10-22, language-source.md. These are four independent
// source equivalence premises, not an admission certificate. V1-V6 retain their
// meanings. Arrays/keys, callee/receiver, event order and source scope still
// require their own proofs before any native form may be published.
constexpr bool TaggedTemplates(SourceContract c) { return c == SourceContract::kLanguageSourceV7 || c == SourceContract::kLanguageSourceV8 || c == SourceContract::kLanguageSourceV9 || c == SourceContract::kLanguageSourceV10; }
constexpr bool ObjectSpreads(SourceContract c) { return c == SourceContract::kLanguageSourceV7 || c == SourceContract::kLanguageSourceV8 || c == SourceContract::kLanguageSourceV9 || c == SourceContract::kLanguageSourceV10; }
constexpr bool ObjectRests(SourceContract c) { return c == SourceContract::kLanguageSourceV7 || c == SourceContract::kLanguageSourceV8 || c == SourceContract::kLanguageSourceV9 || c == SourceContract::kLanguageSourceV10; }
constexpr bool NativeNewTarget(SourceContract c) { return c == SourceContract::kLanguageSourceV7 || c == SourceContract::kLanguageSourceV8 || c == SourceContract::kLanguageSourceV9 || c == SourceContract::kLanguageSourceV10; }

// Approved 2026-10-22: ISA/C++/AOT argument layout for wide calls. Only
// WIDE_CALLRANGE differs in the pinned asm stub; identity/order still require proof.
constexpr bool WideRangeCalls(SourceContract c) { return c == SourceContract::kLanguageSourceV8 || c == SourceContract::kLanguageSourceV9 || c == SourceContract::kLanguageSourceV10; }

// Approved 2026-10-23; four independent Ark-faithful premises. Each still
// needs its construction, reference, scope and completion certificate.
constexpr bool ComputedFunctionNames(SourceContract c) { return c == SourceContract::kLanguageSourceV10; }
constexpr bool DynamicImports(SourceContract c) { return c == SourceContract::kLanguageSourceV10; }
constexpr bool ModuleNamespaces(SourceContract c) { return c == SourceContract::kLanguageSourceV10; }
constexpr bool ArrayPatternAcquisition(SourceContract c) { return c == SourceContract::kLanguageSourceV10; }

// User approved 2026-10-22: native script globals preserve Ark recompilation;
// global lexical activation and unresolved sloppy writes differ on standard JS.
constexpr bool GlobalScripts(SourceContract contract) {
  return contract == SourceContract::kLanguageSourceV9 || contract == SourceContract::kLanguageSourceV10;
}

}  // namespace abcd::source
#endif
