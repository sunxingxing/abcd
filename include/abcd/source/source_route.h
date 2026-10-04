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
  kTransformStage,
  kContextStage,
  kTreeStage,
  kPrintStage,
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
    case ValueRouteError::kTransformStage: return "transform_stage";
    case ValueRouteError::kContextStage: return "context_stage";
    case ValueRouteError::kTreeStage: return "tree_stage";
    case ValueRouteError::kPrintStage: return "print_stage";
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
// Rules 15 additionally admit a checked private phi as a member-call receiver
// when property and invocation share a block; no edge write may intervene.
// Rules 16 add checked natural-loop syntax and once-only MethodEntry phi copies.
// Rules 18 additionally admit independently proved fresh object initializers.
// Rules 19 additionally admit allocation-site native lexical declarations
// with independent cell identity, scope, initialization and origin checks.
// Rules 20 share tree preparation with occurrence-bound DEFINEFUNC owners.
// Created-function expression publication remains independently gated.
// Rules 21 avoid supplied lexical/global names when allocating private names.
// Rules 22 add checked primitive buffered-object properties in the common AST.
// Rules 23 compose primitive buffer prefixes with proved own-write suffixes.
// Rules 24 add closed, immutable function-family emission with per-occurrence
// receipts, fresh destination checks and all-instruction source dispositions.
// Rules 25 retain callable-occurrence input ownership and explicit initialized
// frame-receiver premises, with debugger statements in the common source tree.
// Rules 26 admit independent frame-function reads through value import, while
// source publication remains closed pending an immutable self-binding proof.
// Rules 27 verify immutable named-function self scope for frame-function reads.
// Rules 28 distinguish named-function self from private creation-time binding.
// Rules 29 admit realm identity into value IR, not yet into source publication.
// Rules 30 publish realm AST only after destination binding validation.
// Rules 31 preserve the intrinsic Symbol constructor through semantic/value IR.
// Rules 32 require an explicit host binding when publishing Symbol intrinsic reads.
// Rules 33 forbid rest-signature recovery across implicit argument snapshots.
// Rules 34 preserve wide read occurrences across SSA and value correspondence.
// Rules 35 validate the canonical argument window before rest recovery.
// Rules 36 retain argument snapshot slot dependencies and require owner proof.
// Rules 37 retain checked invocation argument facts from exact callable owners.
// Rules 42 preserve arrow ABI through explicit own-invocation method carriers.
// Rules 44 admit explicit throw through common value IR, AST and printer.
// Rules 45 admit occurrence-bound standalone try/catch through original phased
// input, shared tree correspondence, same-file constants and callable ABI.
// Rules 50 verify phased expression effect intervals and final throw owners.
// Rules 51 publish guarded external module reads only into a same-input native
// ESM enclosure, rechecking owner, slot, spelling and destination shadows.
// Rules 52 recover fresh own-data initialization across unique normal phase
// corridors, retaining canonical RHS snapshots and rejecting early observers.
// Rules 53 compose verified module load/guard pairs as ordered phased subtrees,
// preserving both original effects, catch snapshots and source placements.
// Rules 54 require closure-write evidence for unresolved buffered methods in
// cell initialization. Phased evaluation uses completed-store witnesses; source
// trees with phased cells require the exception-lifetime bridge.
// Rules 55 admit local phase cells through checked try/catch source lifetimes,
// original completed-store load facts and independent tree placement checks.
// Rules 56 partition rejected phased expression trees in one bounded retry,
// retaining legal inner effects without relaxing final-root exception checks.
// Rules 57 carry occurrence-bound phased roots through shared immutable family
// preparation/emission, retaining destination, store and original-PC gates.
// Rules 58 verify native module destinations across immutable function families,
// retaining file-backed member records and rechecking lexical ancestry on reuse.
// Rules 59 admit iterator-result creation through shared value/tree stages,
// checking ordered own data properties and the runtime's exact true flag.
// Rules 60 require class-creation constructor identity for apply construction,
// verify private complete array arguments and retain original effect order.
// Rules 61 compose input-backed iterator records through checked literal and
// phased expression intervals, retaining identity, operands and catch scopes.
// Rules 62 bind nested phased member references to their original call spine,
// retaining getter order, argument availability, receivers and catch scopes.
// Rules 63 admit creation-proven direct functions to the shared apply
// constructor path; child bodies, array ownership and order remain mandatory.
// Rules 64 preserve actual class-member input failures as explicit omissions
// in drafts; their absence never certifies bodies or executable publication.
// Rules 65 bind parent-initialized module reads through phased method input,
// live module aliases, unchanged Before/Commit events and family-write checks.
// Rules 66 use common expression capabilities for single-evaluation snapshots
// and rely on verified value/effect ownership for References, not CFG colocation.
// Rules 67 derive native rest ownership from CFG effects and SSA argument
// transports; no fixed MOV/COPYRESTARGS/STA prologue permits the signature.
// Rules 68 share retained value storage semantics between call receivers and
// selection arms, including prior phi results without replaying their producer.
// Rules 69 preserve canonical retained inputs on phased selection edges and
// allow invariant pure evaluations across CFG splits without mutable bindings.
// Rules 77 bind child module writes using immutable initialized-entry premises,
// canonical native aliases and actual normal/phased store operands; no new declaration.
// Rules 103 project constructor tail returns of their own receiver through
// native completion, retaining tree/control/origins and protected reads.
// Rules 104 use a null/undefined/other abstract interpretation for optional
// member syntax; original tree/control and omitted predicate origins remain.
// Rules 105 share nullish value/predicate outcomes for native ?? and optional
// member syntax, retaining opaque fallback effects and unchanged tree origins.
// Rules 106 project retained SSA values through shared finite truth/nullish
// domains; eager snapshot ownership and original effect/exception sites remain.
// Rules 107 carry reference formation and ordered reads through nullish syntax;
// retained assignments become native bases without replaying their RHS effects.
// Rules 108 retain conditional statement roots through flat predicate staging.
// Rules 109 bind an explicit language arithmetic premise through common value rules.
// Rules 110 share normal block placement with decision predicates and prefixes.
// Rules 111 share verified cell initialization with normal literal ownership.
// Rules 112 share Boolean results, linear prefixes and initialized cell uses
// with conditional-call expression ownership; effect/Reference proofs remain.
// Rules 113 share exact copy/phi identity projection between normal and phased
// evaluation; computation ownership and transport anchors remain distinct.
// Rules 114 prove context-free initializer values through common expression
// capabilities and exact SSA identity, independent of producer/block layout.
inline constexpr std::uint32_t kRouteEvidenceRules = 114;
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
