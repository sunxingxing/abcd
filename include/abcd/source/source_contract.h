#ifndef ABCD_SOURCE_SOURCE_CONTRACT_H_
#define ABCD_SOURCE_SOURCE_CONTRACT_H_

#include <cstdint>
#include <exception>
#include <string_view>

#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/catch_payload_domain.h"

namespace abcd::source {
enum class SourceContract : std::uint8_t {
  kLanguageAndHostV1, kLanguageAndHostV2, kLanguageAndHostV3, kLanguageArithmeticV1, kLanguageNumericV1,
  kLanguageConstructorsV1, kLanguageArithmeticConstructorsV1, kLanguageNumericConstructorsV1,
  kLanguageSourceV1, kLanguageSourceV2, kLanguageSourceV3, kLanguageSourceV4
};
enum class ConstructorSemantics : std::uint8_t { kPinnedRuntime, kSourceLanguage };
enum class ConstructorForwardingSemantics : std::uint8_t { kInvocationFrame, kSourceRestSpread };
enum class RealmBindingPolicy : std::uint8_t { kExplicitHost, kStableGlobalThis };
enum class ResourceFailureScope : std::uint8_t { kObservable, kExcluded };
enum class CallableTextPolicy : std::uint8_t { kUnspecified, kRecovered };
enum class CaptureReadSemantics : std::uint8_t { kPinnedRuntime, kSourceTdz };
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

// User-approved 2026-10-13: explicit rest/spread may observe array iteration;
// invocation identity, argument values and the retained body still require proof.
// This premise is independent of the earlier native-super constructor opt-in.
constexpr ConstructorForwardingSemantics ConstructorForwarding(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
      return ConstructorForwardingSemantics::kInvocationFrame;
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
      return ConstructorForwardingSemantics::kSourceRestSpread;
  }
  std::terminate();
}

// Opt-in language arithmetic changes the numeric equivalence premise only;
// it does not claim measured equivalence to a particular Ark binary. See
// language-arithmetic.md. LanguageNumericV1 also covers unary numeric rules,
// explicitly approved on 2026-10-06; see language-numeric.md. Neither opt-in
// changes default V3 or constructor/host premises.
constexpr analysis::ArithmeticSemantics Arithmetic(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageConstructorsV1:
      return analysis::ArithmeticSemantics::kPinnedRuntime;
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
      return analysis::ArithmeticSemantics::kSourceNumeric;
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
      return analysis::ArithmeticSemantics::kSourceLanguage;
  }
  std::terminate();
}
// Explicit user-approved language super semantics, independent of arithmetic.
// No assumption about lifetime parent brand; current parent/newTarget are native.
constexpr ConstructorSemantics Constructors(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1: return ConstructorSemantics::kPinnedRuntime;
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return ConstructorSemantics::kSourceLanguage;
  }
  std::terminate();
}
constexpr SourceContract WithLanguageConstructors(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV3: return SourceContract::kLanguageConstructorsV1;
    case SourceContract::kLanguageArithmeticV1: return SourceContract::kLanguageArithmeticConstructorsV1;
    case SourceContract::kLanguageNumericV1: return SourceContract::kLanguageNumericConstructorsV1;
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return contract;
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2: std::terminate(); // Historical host profiles cannot opt in.
  }
  std::terminate();
}
constexpr analysis::PendingExceptionValues PendingExceptions(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
      return analysis::PendingExceptionValues::kLanguage;
  }
  std::terminate();
}
constexpr analysis::CallResultValues NormalCallResults(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return analysis::CallResultValues::kUnknown;
    case SourceContract::kLanguageAndHostV2: return analysis::CallResultValues::kLanguage;
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return analysis::CallResultValues::kLanguage;
  }
  std::terminate();
}
constexpr RealmBindingPolicy RealmBinding(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return RealmBindingPolicy::kExplicitHost;
    case SourceContract::kLanguageAndHostV2: return RealmBindingPolicy::kStableGlobalThis;
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return RealmBindingPolicy::kStableGlobalThis;
  }
  std::terminate();
}
constexpr ResourceFailureScope ResourceFailures(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return ResourceFailureScope::kObservable;
    case SourceContract::kLanguageAndHostV2: return ResourceFailureScope::kExcluded;
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return ResourceFailureScope::kExcluded;
  }
  std::terminate();
}
constexpr CallableTextPolicy CallableText(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
      return CallableTextPolicy::kUnspecified;
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4:
      return CallableTextPolicy::kRecovered;
  }
  std::terminate();
}
// LanguageSourceV1 = LanguageNumericConstructorsV1 plus four source premises
// explicitly approved by the user on 2026-10-13 (language-source.md). Each
// premise has its own predicate; never test the enum value directly.
// Captured accesses (unguarded reads, writes) are source identifier accesses:
// a function may be created before the binding's first store (es2panda omits
// the check by textual order), and only an early call differs (the runtime
// reads the hole or writes the slot, the source throws ReferenceError).
// Reads approved 2026-10-13, extended to writes the same day.
constexpr CaptureReadSemantics CaptureReads(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1: return CaptureReadSemantics::kPinnedRuntime;
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return CaptureReadSemantics::kSourceTdz;
  }
  std::terminate();
}
// Round 31: ArkTS preserves lazy initialization; array spread is a separate
// approved premise whose lowering is not enabled by the lazy-import adapter.
enum class LazyImportSemantics : std::uint8_t { kPinnedRuntime, kArkTsLazyImport };
enum class ArraySpreadSemantics : std::uint8_t { kPinnedRuntime, kSourceIterator };
constexpr LazyImportSemantics LazyImports(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
      return LazyImportSemantics::kPinnedRuntime;
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return LazyImportSemantics::kArkTsLazyImport;
  }
  std::terminate();
}
constexpr ArraySpreadSemantics ArraySpreads(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
      return ArraySpreadSemantics::kPinnedRuntime;
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return ArraySpreadSemantics::kSourceIterator;
  }
  std::terminate();
}
// Approved 2026-10-14. V2 adds only standard iterator acquisition; V1 keeps
// its historical four premises. This does not authorize apply/spread calls,
// next re-reads, different close completion priority, or binding changes.
enum class IterationAcquisitionSemantics : std::uint8_t { kPinnedRuntime, kStandardV1 };
constexpr IterationAcquisitionSemantics IterationAcquisition(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1: return IterationAcquisitionSemantics::kPinnedRuntime;
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return IterationAcquisitionSemantics::kStandardV1;
  }
  std::terminate();
}
// Approved 2026-10-15: Ark output policies preserve kind and metadata, never
// erase concurrent/sendable/shared semantics. Each policy has independent
// admission; publication requires completion and recompilation proof.
enum class ConcurrentOutputPolicy : std::uint8_t { kNotEnabled, kArkDirectiveV1 };
constexpr ConcurrentOutputPolicy ConcurrentOutput(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
      return ConcurrentOutputPolicy::kNotEnabled;
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return ConcurrentOutputPolicy::kArkDirectiveV1;
  }
  std::terminate();
}
enum class SendableOutputPolicy : std::uint8_t { kNotEnabled, kArkDirectiveV1 };
constexpr SendableOutputPolicy SendableOutput(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
      return SendableOutputPolicy::kNotEnabled;
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return SendableOutputPolicy::kArkDirectiveV1;
  }
  std::terminate();
}
enum class SharedModuleOutputPolicy : std::uint8_t { kNotEnabled, kArkDirectiveV1 };
constexpr SharedModuleOutputPolicy SharedModuleOutput(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
      return SharedModuleOutputPolicy::kNotEnabled;
    case SourceContract::kLanguageSourceV3:
    case SourceContract::kLanguageSourceV4: return SharedModuleOutputPolicy::kArkDirectiveV1;
  }
  std::terminate();
}

// Approved 2026-10-16: only externally sourced exception payloads exclude
// Hole. Local explicit throws retain their SSA domain, including raw Hole.
constexpr analysis::ExternalExceptionPolicy ExternalExceptionNoHole(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1:
    case SourceContract::kLanguageAndHostV2:
    case SourceContract::kLanguageAndHostV3:
    case SourceContract::kLanguageArithmeticV1:
    case SourceContract::kLanguageNumericV1:
    case SourceContract::kLanguageConstructorsV1:
    case SourceContract::kLanguageArithmeticConstructorsV1:
    case SourceContract::kLanguageNumericConstructorsV1:
    case SourceContract::kLanguageSourceV1:
    case SourceContract::kLanguageSourceV2:
    case SourceContract::kLanguageSourceV3:
      return analysis::ExternalExceptionPolicy::kPinnedRuntime;
    case SourceContract::kLanguageSourceV4: return analysis::ExternalExceptionPolicy::kNoHoleV1;
  }
  std::terminate();
}

// Serialization boundary only. Never dispatch engine semantics on this label.
constexpr std::string_view SourceContractName(SourceContract contract) {
  switch (contract) {
    case SourceContract::kLanguageAndHostV1: return "language_and_host_v1";
    case SourceContract::kLanguageAndHostV2: return "language_and_host_v2";
    case SourceContract::kLanguageNumericV1: return "language_numeric_v1";
    case SourceContract::kLanguageConstructorsV1: return "language_constructors_v1";
    case SourceContract::kLanguageArithmeticConstructorsV1: return "language_arithmetic_constructors_v1";
    case SourceContract::kLanguageNumericConstructorsV1: return "language_numeric_constructors_v1";
    case SourceContract::kLanguageArithmeticV1: return "language_arithmetic_v1";
    case SourceContract::kLanguageAndHostV3: return "language_and_host_v3";
    case SourceContract::kLanguageSourceV1: return "language_source_v1";
    case SourceContract::kLanguageSourceV2: return "language_source_v2";
    case SourceContract::kLanguageSourceV3: return "language_source_v3";
    case SourceContract::kLanguageSourceV4: return "language_source_v4";
  }
  std::terminate();
}
}  // namespace abcd::source
#endif
