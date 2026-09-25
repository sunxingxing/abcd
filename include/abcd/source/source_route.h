#ifndef ABCD_SOURCE_SOURCE_ROUTE_H_
#define ABCD_SOURCE_SOURCE_ROUTE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "abcd/abc/header.h"
#include "abcd/abc/method_body.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/source_contract.h"

namespace abcd::source {
// Source route identity (docs/design/source-recovery/acceptance.md §2).
// RoutePolicy selects which pipeline may publish a method body; SourceRoute
// records which one actually ran. A route receipt without an explicit route
// is never interpreted as the value-tree route (T00/A01: legacy methods that
// predate receipts stay Legacy).
enum class RoutePolicy : std::uint8_t {
  kLegacyAllowed = 0,
  kRequireValueTree = 1,
};
enum class SourceRoute : std::uint8_t {
  kLegacy = 0,
  kValueTree = 1,
};
// Unified stage ladder. The legacy route may claim Decoded, InputVerified,
// Printed and Published only; Values* through TreeVerified are value-tree
// stages that the legacy pipeline must never claim.
enum class SourceStage : std::uint8_t {
  kDecoded = 0,
  kInputVerified = 1,
  kValuesImported = 2,
  kValuesVerified = 3,
  kControlFrozen = 4,
  kBindingsFrozen = 5,
  kTreeVerified = 6,
  kPrinted = 7,
  kPublished = 8,
};
enum class Coverage : std::uint8_t {
  kComplete = 0,
  kIncomplete = 1,
  kUnsupported = 2,
};
enum class RouteFailureClass : std::uint8_t {
  kUnsupported = 0,
  kUnproved = 1,
  kInvalidInput = 2,
  kInvariantViolation = 3,
  kWorkLimit = 4,
  kMemoryLimit = 5,
  kOutputFailure = 6,
};
// Typed reasons owned by the value-tree route itself. Legacy failure enums
// are reported through their own result variants and join receipts only when
// a task defines their mapping; never overload SourceError here.
enum class ValueRouteError : std::uint8_t {
  // No value-tree stage is implemented past route admission yet (T00).
  kRouteUnavailable = 0,
  kInputStage,
  kImportStage,
};
struct RouteFailure {
  RouteFailureClass failure_class;
  ValueRouteError reason;
  std::optional<abc::CodeOffset> pc = {};
};

// Typed identity binding route evidence to the exact input it describes
// (T00 correction R3). Reuses the existing content-hash, ABC-version, profile
// and source-contract types: the same numeric method in another file, or the
// same file under another contract, is a different identity. profile is the
// one combined container/ISA/semantic enum the engine currently pins; it is
// recorded together with the raw abc_version bytes and must not be presented
// as three independently certified profile identities.
struct RouteInputIdentity {
  corpus::Sha256 file_hash{};
  std::uint64_t file_size = 0;
  abc::AbcVersion abc_version{};
  abc::ProfileId profile = abc::ProfileId::kUnknown;
  SourceContract contract = kSourceContract;
  auto operator<=>(const RouteInputIdentity&) const = default;
};
[[nodiscard]] RouteInputIdentity MakeRouteInputIdentity(
    const corpus::FileFingerprint& fingerprint, const abc::Header& header);

// Per-method (or per-record when method is absent) route receipt. Stage
// evidence is signed by the stage that reached it; nullopt records that no
// route stage ran. Serializers must not invent stages or coverage.
// work_revision binds a receipt to the working-IR revision it was produced
// from; it is absent before ValuesImported and mandatory on working products.
// Legacy products never carry a working revision (schema 3).
struct MethodRouteReceipt {
  RouteInputIdentity identity{};
  std::optional<abc::EntityOffset> method = {};
  std::optional<abc::EntityOffset> record = {};
  SourceRoute route = SourceRoute::kLegacy;
  std::optional<SourceStage> stage = {};
  Coverage coverage = Coverage::kUnsupported;
  bool legacy_writer_used = false;
  std::optional<RouteFailure> failure = {};
  std::optional<std::uint64_t> work_revision = {};
};

// Boundary label names; serialization only, engine decisions use the enums.
constexpr std::string_view RoutePolicyName(RoutePolicy policy) {
  switch (policy) {
    case RoutePolicy::kLegacyAllowed: return "legacy_allowed";
    case RoutePolicy::kRequireValueTree: return "require_value_tree";
  }
  return {};
}
constexpr std::string_view SourceRouteName(SourceRoute route) {
  switch (route) {
    case SourceRoute::kLegacy: return "legacy";
    case SourceRoute::kValueTree: return "value_tree";
  }
  return {};
}
constexpr std::string_view SourceStageName(SourceStage stage) {
  switch (stage) {
    case SourceStage::kDecoded: return "decoded";
    case SourceStage::kInputVerified: return "input_verified";
    case SourceStage::kValuesImported: return "values_imported";
    case SourceStage::kValuesVerified: return "values_verified";
    case SourceStage::kControlFrozen: return "control_frozen";
    case SourceStage::kBindingsFrozen: return "bindings_frozen";
    case SourceStage::kTreeVerified: return "tree_verified";
    case SourceStage::kPrinted: return "printed";
    case SourceStage::kPublished: return "published";
  }
  return {};
}
constexpr std::string_view CoverageName(Coverage coverage) {
  switch (coverage) {
    case Coverage::kComplete: return "complete";
    case Coverage::kIncomplete: return "incomplete";
    case Coverage::kUnsupported: return "unsupported";
  }
  return {};
}
constexpr std::string_view RouteFailureClassName(RouteFailureClass kind) {
  switch (kind) {
    case RouteFailureClass::kUnsupported: return "unsupported";
    case RouteFailureClass::kUnproved: return "unproved";
    case RouteFailureClass::kInvalidInput: return "invalid_input";
    case RouteFailureClass::kInvariantViolation: return "invariant_violation";
    case RouteFailureClass::kWorkLimit: return "work_limit";
    case RouteFailureClass::kMemoryLimit: return "memory_limit";
    case RouteFailureClass::kOutputFailure: return "output_failure";
  }
  return {};
}
constexpr std::string_view ValueRouteErrorName(ValueRouteError reason) {
  switch (reason) {
    case ValueRouteError::kRouteUnavailable: return "route_unavailable";
    case ValueRouteError::kInputStage: return "input_stage";
    case ValueRouteError::kImportStage: return "import_stage";
  }
  return {};
}

// Admission decision consulted by every entry that can reach the legacy
// writer. When legacy is not permitted the caller must surface
// value_route_failure as its typed product and must not run the legacy
// pipeline, in whole or in part (A01: no legacy body may replace a refused
// new-route attempt).
//
// Invocation-context rule: RoutePolicy is decided once per invocation at a
// guarded entry (BuildSemanticBody, BuildSemanticTemplate, BuildEntrySource,
// BuildRecordSource, WriteBaseFunction, WriteBaseFunctionTemplate). Guarded
// entries take the policy as a required argument - no default silently
// resets it - and descendant writers inherit the admitting entry's policy by
// construction: a refused policy never reaches them, and they must not
// re-admit, downgrade or re-default it. Writers that only ever run inside a
// legacy invocation context state that context explicitly at their calls.
struct RouteAdmission {
  bool legacy_permitted = true;
  std::optional<RouteFailure> value_route_failure = {};
};
[[nodiscard]] RouteAdmission AdmitLegacyWriter(RoutePolicy policy);

// Returned by guarded pipeline entries when policy refuses the legacy route.
struct RouteRefusal {
  RouteFailure failure;
};

// Versioned receipt serialization. Unknown schema versions, unknown labels,
// out-of-range integers, missing fields and receipts identifying neither a
// method nor a record are rejected; a parse result is never defaulted into
// success, and a parsed receipt must still satisfy ValidateRouteReceipt.
inline constexpr unsigned kRouteReceiptSchema = 3;
// Engine/rules provenance of the admission and stage semantics above. Bump
// with any change to them; receipts signed under other rules are rejected
// rather than reinterpreted.
inline constexpr std::uint32_t kRouteEvidenceRules = 3;
enum class RouteReceiptError : std::uint8_t {
  kSyntax = 0,
  kSchema = 1,
  kIdentity = 2,
  kRoute = 3,
  kStage = 4,
  kCoverage = 5,
  kFailure = 6,
  kWriter = 7,
  kContradiction = 8,
  kEvidence = 9,
};
// Central typed receipt validation (T00 correction R2). Producers, the
// boundary parser and obligation closure all consult this one contract; no
// code path may certify a receipt state that the reached stages do not prove:
// - Legacy receipts exist only for legacy-writer text: stage Printed or
//   Published, legacy_writer_used set, no failure (legacy failures are not
//   mapped into receipts yet) and coverage Complete or Incomplete.
// - ValueTree receipts never used the legacy writer. Coverage Unsupported
//   requires the typed failure that refused it; Complete requires an emitted
//   product (stage at least Printed) and no failure. A TreeVerified-only
//   artifact is Incomplete, never Complete.
[[nodiscard]] std::optional<RouteReceiptError> ValidateRouteReceipt(
    const MethodRouteReceipt& receipt);
// Invalid evidence is a typed boundary failure, never a JSON receipt.
[[nodiscard]] std::variant<std::string, RouteReceiptError>
SerializeRouteReceipt(const MethodRouteReceipt& receipt);
[[nodiscard]] std::variant<MethodRouteReceipt, RouteReceiptError>
ParseRouteReceipt(std::string_view text);

// Monotonic route-aware stage ledger. Each route may only walk its own
// ordered stage sequence (legacy: Decoded, InputVerified, Printed, Published;
// value tree: the full ladder), one step at a time from its first stage: a
// legacy ledger can never claim a value stage, and no ledger may jump
// straight to a terminal stage. Each Advance call is made by the owner of
// that stage after it succeeded, not by the serializer.
class RouteStageLedger {
 public:
  explicit RouteStageLedger(SourceRoute route) : route_(route) {}
  [[nodiscard]] bool Advance(SourceStage stage);
  [[nodiscard]] std::optional<SourceStage> reached() const { return reached_; }
  [[nodiscard]] SourceRoute route() const { return route_; }

 private:
  SourceRoute route_;
  std::optional<SourceStage> reached_ = {};
};

// Child-method route obligations (A02). The obligation names the expected
// input identity as well as the method: closure requires a receipt that is
// valid under ValidateRouteReceipt, matches both identities and is Complete
// on a route the policy accepts (RequireValueTree accepts only ValueTree
// children; LegacyAllowed accepts a valid completed Legacy or ValueTree
// child - it is not RequireLegacy). A missing child receipt, a child on the
// wrong route, an incomplete or unsupported child, a failed child or a
// receipt from another input leaves the parent's obligations open.
struct ChildRouteObligation {
  abc::EntityOffset method{0};
  RouteInputIdentity identity{};
  std::optional<MethodRouteReceipt> receipt = {};
};
[[nodiscard]] bool RouteObligationsClosed(
    RoutePolicy policy, std::span<const ChildRouteObligation> obligations);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SOURCE_ROUTE_H_
