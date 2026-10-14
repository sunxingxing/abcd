#include "tree_print_failure_json.h"
#include "guarded_call_json.h"
#include "abcd/source/source_route.h"

#include <nlohmann/json.hpp>
#include "control_failure_json.h"
#include "tree_failure_json.h"
#include "evaluation_failure_json.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace abcd::source {
namespace {
using Json = nlohmann::json;

// The value-tree stage sequence; walked in order, never jumped.
constexpr std::array kValueStages{
    SourceStage::kDecoded,        SourceStage::kInputVerified,
    SourceStage::kValuesImported, SourceStage::kValuesVerified,
    SourceStage::kControlFrozen,  SourceStage::kBindingsFrozen,
    SourceStage::kTreeVerified,   SourceStage::kPrinted,
    SourceStage::kPublished};
constexpr std::array kProfiles{
    abc::ProfileId::kUnknown,     abc::ProfileId::kDynamic9,
    abc::ProfileId::kDynamic1102, abc::ProfileId::kDynamic1202,
    abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301,
    abc::ProfileId::kDynamic2400, abc::ProfileId::kStatic0107};
// Derived from the single serialization map, indexed by the dense enum, so
// every contract that can be written can also be read back.
constexpr std::size_t kContractCount = static_cast<std::size_t>(SourceContract::kLanguageSourceV2) + 1;
constexpr auto kContractNames = [] {
  std::array<std::string_view, kContractCount> names{};
  for (std::size_t i = 0; i < kContractCount; ++i) names[i] = SourceContractName(static_cast<SourceContract>(i));
  return names;
}();
constexpr std::string_view kRouteNames[]{"value_tree"};
constexpr std::string_view kStageNames[]{
    "decoded", "input_verified", "values_imported", "values_verified",
    "control_frozen", "bindings_frozen", "tree_verified", "printed",
    "published"};
constexpr std::string_view kCoverageNames[]{"complete", "incomplete",
                                            "unsupported"};
constexpr std::string_view kFailureClassNames[]{
    "unsupported", "unproved", "invalid_input", "invariant_violation",
    "work_limit", "memory_limit", "output_failure"};
constexpr std::string_view kFailureReasonNames[]{"input_stage", "import_stage", "transform_stage", "context_stage", "tree_stage", "print_stage"};
constexpr std::size_t kSha256TextBytes = 64;

// Exact-range unsigned access (T00 correction R4): every integer field is
// validated against its declared width before conversion, so an oversized
// JSON number such as 4294967297 is rejected instead of narrowing to a
// valid-looking offset.
template <typename UInt>
std::optional<UInt> ParseUnsigned(const Json& value) {
  static_assert(std::is_unsigned_v<UInt>);
  if (!value.is_number_unsigned()) return {};
  const auto raw = value.get<std::uint64_t>();
  if (raw > std::numeric_limits<UInt>::max()) return {};
  return static_cast<UInt>(raw);
}

// Label-to-enum lookups reject unknown labels instead of defaulting; the
// caller converts a nullopt into the matching RouteReceiptError.
template <typename Enum, std::size_t kCount>
std::optional<Enum> ParseLabel(std::string_view label,
                               const std::string_view (&names)[kCount]) {
  for (std::size_t i = 0; i < kCount; ++i)
    if (names[i] == label) return static_cast<Enum>(i);
  return {};
}
template <typename Enum, std::size_t kCount>
std::optional<Enum> ParseLabel(std::string_view label, const std::array<std::string_view, kCount>& names) {
  for (std::size_t i = 0; i < kCount; ++i)
    if (names[i] == label) return static_cast<Enum>(i);
  return {};
}

std::optional<corpus::Sha256> ParseSha256(std::string_view text) {
  if (text.size() != kSha256TextBytes) return {};
  corpus::Sha256 hash;
  for (std::size_t i = 0; i < hash.bytes.size(); ++i) {
    std::uint8_t byte = 0;
    for (std::size_t half = 0; half < 2; ++half) {
      const auto digit = text[2 * i + half];
      std::uint8_t value;
      if (digit >= '0' && digit <= '9')
        value = static_cast<std::uint8_t>(digit - '0');
      else if (digit >= 'a' && digit <= 'f')
        value = static_cast<std::uint8_t>(digit - 'a' + 10);
      else
        return {};
      byte = static_cast<std::uint8_t>(byte << 4 | value);
    }
    hash.bytes[i] = byte;
  }
  return hash;
}

std::string Sha256Text(const corpus::Sha256& hash) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string text;
  text.reserve(kSha256TextBytes);
  for (const auto byte : hash.bytes) {
    text += digits[byte >> 4];
    text += digits[byte & 0xf];
  }
  return text;
}
}  // namespace

RouteInputIdentity MakeRouteInputIdentity(
    const corpus::FileFingerprint& fingerprint, const abc::Header& header) {
  return RouteInputIdentity{fingerprint.sha256, fingerprint.size,
                            header.version, header.profile, kSourceContract};
}

std::optional<RouteReceiptError> ValidateRouteReceipt(
    const MethodRouteReceipt& receipt) {
  if (!receipt.method && !receipt.record) return RouteReceiptError::kIdentity;
  const auto& identity = receipt.identity;
  if (identity.file_size < abc::kDynamicHeaderSize ||
      identity.profile == abc::ProfileId::kUnknown ||
      std::find(kProfiles.begin(), kProfiles.end(), identity.profile) ==
          kProfiles.end() ||
      identity.abc_version == abc::AbcVersion{} ||
      (identity.contract != SourceContract::kLanguageAndHostV1 &&
       identity.contract != SourceContract::kLanguageAndHostV2 &&
       identity.contract != SourceContract::kLanguageAndHostV3 &&
       identity.contract != SourceContract::kLanguageArithmeticV1 &&
       identity.contract != SourceContract::kLanguageNumericV1 &&
       identity.contract != SourceContract::kLanguageConstructorsV1 &&
       identity.contract != SourceContract::kLanguageArithmeticConstructorsV1 &&
       identity.contract != SourceContract::kLanguageNumericConstructorsV1 &&
       identity.contract != SourceContract::kLanguageSourceV1 &&
       identity.contract != SourceContract::kLanguageSourceV2))
    return RouteReceiptError::kIdentity;
  const auto valid_entity = [&](const auto& entity) {
    return !entity || (entity->value >= abc::kDynamicHeaderSize &&
                      entity->value < identity.file_size);
  };
  if (!valid_entity(receipt.method) || !valid_entity(receipt.record))
    return RouteReceiptError::kIdentity;
  if (receipt.stage &&
      std::find(kValueStages.begin(), kValueStages.end(), *receipt.stage) ==
          kValueStages.end())
    return RouteReceiptError::kStage;
  const bool has_work = receipt.route == SourceRoute::kValueTree &&
      receipt.stage && *receipt.stage >= SourceStage::kValuesImported;
  if (has_work != receipt.work_revision.has_value()) return RouteReceiptError::kSchema;
  if (receipt.coverage != Coverage::kComplete &&
      receipt.coverage != Coverage::kIncomplete &&
      receipt.coverage != Coverage::kUnsupported)
    return RouteReceiptError::kCoverage;
  switch (receipt.route) {
    case SourceRoute::kValueTree:
      if (receipt.failure) {
        // A refusal carries its typed reason and claims no product.
        if (receipt.coverage != Coverage::kUnsupported)
          return RouteReceiptError::kCoverage;
        if (static_cast<unsigned>(receipt.failure->failure_class) >=
            std::size(kFailureClassNames)) return RouteReceiptError::kFailure;
        switch (receipt.failure->reason) {
          case ValueRouteError::kInputStage:
            if (receipt.stage != SourceStage::kDecoded) return RouteReceiptError::kStage;
            break;
          case ValueRouteError::kImportStage:
            if (receipt.stage != SourceStage::kInputVerified) return RouteReceiptError::kStage;
            break;
          case ValueRouteError::kTransformStage:
            if (receipt.stage != SourceStage::kValuesImported) return RouteReceiptError::kStage;
            break;
          case ValueRouteError::kContextStage:
            if (receipt.stage != SourceStage::kDecoded) return RouteReceiptError::kStage;
            break;
          case ValueRouteError::kTreeStage:
            if (receipt.stage != SourceStage::kValuesVerified &&
                receipt.stage != SourceStage::kControlFrozen &&
                receipt.stage != SourceStage::kBindingsFrozen)
              return RouteReceiptError::kStage;
            break;
          case ValueRouteError::kPrintStage:
            if (receipt.stage != SourceStage::kTreeVerified) return RouteReceiptError::kStage;
            break;
          default: return RouteReceiptError::kFailure;
        }
        return {};
      }
      switch (receipt.coverage) {
        case Coverage::kUnsupported:
          // Unsupported without a typed reason hides the capability gap; it
          // never closes obligations.
          return RouteReceiptError::kCoverage;
        case Coverage::kIncomplete:
          return {};
        case Coverage::kComplete:
          // Only an emitted product is complete; TreeVerified alone stays
          // Incomplete, separately represented by its stage.
          if (!receipt.stage || *receipt.stage < SourceStage::kPrinted)
            return RouteReceiptError::kStage;
          return {};
      }
      return RouteReceiptError::kCoverage;
  }
  return RouteReceiptError::kRoute;
}

bool RouteStageLedger::Advance(SourceStage stage) {
  if (route_ != SourceRoute::kValueTree) return false;
  const std::span<const SourceStage> sequence{kValueStages};
  auto next = sequence.begin();
  if (reached_) {
    next = std::find(sequence.begin(), sequence.end(), *reached_);
    if (next == sequence.end()) return false;
    ++next;
  }
  if (next == sequence.end() || *next != stage) return false;
  reached_ = stage;
  return true;
}

bool RouteObligationsClosed(
    std::span<const ChildRouteObligation> obligations) {
  for (const auto& obligation : obligations) {
    if (!obligation.receipt) return false;
    const auto& receipt = *obligation.receipt;
    if (receipt.method != obligation.method) return false;
    // Evidence binds to the expected input context, not just the method
    // number (T00 correction R3).
    if (!(receipt.identity == obligation.identity)) return false;
    if (ValidateRouteReceipt(receipt)) return false;
    if (receipt.coverage != Coverage::kComplete) return false;
    if (receipt.route != SourceRoute::kValueTree) return false;
  }
  return true;
}

std::variant<std::string, RouteReceiptError> SerializeRouteReceipt(
    const MethodRouteReceipt& receipt) {
  if (const auto error = ValidateRouteReceipt(receipt)) return *error;
  Json failure = nullptr;
  if (receipt.failure) {
    failure = Json{{"class", RouteFailureClassName(
                                 receipt.failure->failure_class)},
                   {"reason", ValueRouteErrorName(receipt.failure->reason)},
                   {"pc", receipt.failure->pc
                              ? Json(receipt.failure->pc->value)
                              : Json(nullptr)}};
    // Schema 5: the typed cause chain, outermost first, last entry the leaf.
    Json causes = Json::array();
    for (const auto& cause : receipt.failure->causes.View())
      causes.push_back(Json{{"domain", cause.domain}, {"value", cause.value}});
    failure["causes"] = std::move(causes);
    failure["causes_truncated"] = receipt.failure->causes.truncated;
    if (receipt.failure->printing) failure["printing"] = TreePrintFailureJson(*receipt.failure->printing);
    if (receipt.failure->evaluation) failure["evaluation"] = EvaluationFailureJson(*receipt.failure->evaluation);
    if (receipt.failure->tree) failure["tree"] = TreeFailureJson(*receipt.failure->tree);
    if (receipt.failure->deferred) failure["deferred"] = DeferredCellJson(*receipt.failure->deferred);
    if (receipt.failure->control) failure["control"] = ControlJson(*receipt.failure->control);
    if (receipt.failure->capture) {
      const auto& use = *receipt.failure->capture;
      auto& capture = failure["capture"];
      capture = Json{{"ordinal", use.ordinal}, {"depth", use.cell.depth.value},
          {"slot", use.cell.slot.value}, {"requested", static_cast<unsigned>(use.requested)}};
      if (use.source) {
        capture["method"] = use.source->method.value;
        if (use.source->pc) capture["pc"] = use.source->pc->value;
        if (use.source->working_pc) capture["working_pc"] = use.source->working_pc->value;
      }
    }
    if (receipt.failure->call_reference) {
      const auto& evidence = *receipt.failure->call_reference;
      auto& reference = failure["call_reference"];
      reference = Json{{"kind", static_cast<unsigned>(evidence.kind)}, {"call", evidence.call.value}};
      if (evidence.property) reference["property"] = evidence.property->value;
      if (evidence.placement) reference["placement"] = static_cast<unsigned>(*evidence.placement);
      if (evidence.temporary) reference["temporary"] = static_cast<unsigned>(*evidence.temporary);
      if (evidence.consumer) reference["consumer"] = evidence.consumer->value;
      if (evidence.checked) reference["checked"] = GuardedCallJson(*evidence.checked);
    }
    if (receipt.failure->method) failure["method"] = receipt.failure->method->value;
    if (receipt.failure->working_pc) failure["working_pc"] = receipt.failure->working_pc->value;
    if (receipt.failure->creation_method) failure["creation_method"] = receipt.failure->creation_method->value;
    if (receipt.failure->creation_pc) failure["creation_pc"] = receipt.failure->creation_pc->value;
  }
  Json version = Json::array();
  for (const auto byte : receipt.identity.abc_version.bytes)
    version.push_back(byte);
  return Json{
      {"schema", kRouteReceiptSchema},
      {"evidence_rules", kRouteEvidenceRules},
      {"identity",
       Json{{"sha256", Sha256Text(receipt.identity.file_hash)},
            {"file_size", receipt.identity.file_size},
            {"abc_version", std::move(version)},
            {"profile", static_cast<unsigned>(receipt.identity.profile)},
            {"source_contract",
             SourceContractName(receipt.identity.contract)}}},
      {"work_revision", receipt.work_revision ? Json(*receipt.work_revision)
                                              : Json(nullptr)},
      {"method", receipt.method ? Json(receipt.method->value) : Json(nullptr)},
      {"record", receipt.record ? Json(receipt.record->value) : Json(nullptr)},
      {"route", SourceRouteName(receipt.route)},
      {"stage", receipt.stage ? Json(SourceStageName(*receipt.stage))
                              : Json(nullptr)},
      {"coverage", CoverageName(receipt.coverage)},
      {"failure", std::move(failure)}}
      .dump();
}

std::variant<MethodRouteReceipt, RouteReceiptError> ParseRouteReceipt(
    std::string_view text) {
  Json parsed = Json::parse(text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object())
    return RouteReceiptError::kSyntax;
  // Schema and evidence rules gate the interpretation of every other field.
  const auto schema = parsed.find("schema");
  if (schema == parsed.end() ||
      ParseUnsigned<unsigned>(*schema) != kRouteReceiptSchema)
    return RouteReceiptError::kSchema;
  const auto rules = parsed.find("evidence_rules");
  if (rules == parsed.end() ||
      ParseUnsigned<std::uint32_t>(*rules) != kRouteEvidenceRules)
    return RouteReceiptError::kEvidence;
  MethodRouteReceipt receipt;
  const auto identity = parsed.find("identity");
  if (identity == parsed.end() || !identity->is_object())
    return RouteReceiptError::kIdentity;
  const auto hash = identity->find("sha256");
  if (hash == identity->end() || !hash->is_string())
    return RouteReceiptError::kIdentity;
  if (const auto bytes = ParseSha256(hash->get<std::string>()))
    receipt.identity.file_hash = *bytes;
  else
    return RouteReceiptError::kIdentity;
  const auto file_size = identity->find("file_size");
  if (file_size == identity->end() || !file_size->is_number_unsigned())
    return RouteReceiptError::kIdentity;
  receipt.identity.file_size = file_size->get<std::uint64_t>();
  const auto version = identity->find("abc_version");
  if (version == identity->end() || !version->is_array() ||
      version->size() != receipt.identity.abc_version.bytes.size())
    return RouteReceiptError::kIdentity;
  for (std::size_t i = 0; i < version->size(); ++i) {
    const auto byte = ParseUnsigned<std::uint8_t>((*version)[i]);
    if (!byte) return RouteReceiptError::kIdentity;
    receipt.identity.abc_version.bytes[i] = *byte;
  }
  const auto profile = identity->find("profile");
  std::optional<unsigned> profile_id;
  if (profile != identity->end()) profile_id = ParseUnsigned<unsigned>(*profile);
  if (!profile_id ||
      std::find(kProfiles.begin(), kProfiles.end(),
                static_cast<abc::ProfileId>(*profile_id)) == kProfiles.end())
    return RouteReceiptError::kIdentity;
  receipt.identity.profile = static_cast<abc::ProfileId>(*profile_id);
  const auto contract = identity->find("source_contract");
  if (contract == identity->end() || !contract->is_string())
    return RouteReceiptError::kIdentity;
  if (const auto value = ParseLabel<SourceContract>(
          contract->get<std::string>(), kContractNames))
    receipt.identity.contract = *value;
  else
    return RouteReceiptError::kIdentity;
  // Schema 3 signs working revisions only after successful value import.
  const auto work_revision = parsed.find("work_revision");
  if (work_revision == parsed.end()) return RouteReceiptError::kSchema;
  if (!work_revision->is_null()) {
    receipt.work_revision = ParseUnsigned<std::uint64_t>(*work_revision);
    if (!receipt.work_revision) return RouteReceiptError::kSchema;
  }
  const auto method = parsed.find("method");
  const auto record = parsed.find("record");
  if (method == parsed.end() || record == parsed.end())
    return RouteReceiptError::kIdentity;
  if (!method->is_null()) {
    const auto value = ParseUnsigned<std::uint32_t>(*method);
    if (!value) return RouteReceiptError::kIdentity;
    receipt.method = abc::EntityOffset{*value};
  }
  if (!record->is_null()) {
    const auto value = ParseUnsigned<std::uint32_t>(*record);
    if (!value) return RouteReceiptError::kIdentity;
    receipt.record = abc::EntityOffset{*value};
  }
  if (!receipt.method && !receipt.record)
    return RouteReceiptError::kIdentity;
  const auto route = parsed.find("route");
  if (route == parsed.end() || !route->is_string())
    return RouteReceiptError::kRoute;
  if (const auto value = ParseLabel<SourceRoute>(
          route->get<std::string>(), kRouteNames))
    receipt.route = *value;
  else
    return RouteReceiptError::kRoute;
  const auto stage = parsed.find("stage");
  if (stage == parsed.end()) return RouteReceiptError::kStage;
  if (!stage->is_null()) {
    if (!stage->is_string()) return RouteReceiptError::kStage;
    const auto value =
        ParseLabel<SourceStage>(stage->get<std::string>(), kStageNames);
    if (!value) return RouteReceiptError::kStage;
    receipt.stage = *value;
  }
  const auto coverage = parsed.find("coverage");
  if (coverage == parsed.end() || !coverage->is_string())
    return RouteReceiptError::kCoverage;
  if (const auto value = ParseLabel<Coverage>(
          coverage->get<std::string>(), kCoverageNames))
    receipt.coverage = *value;
  else
    return RouteReceiptError::kCoverage;
  const auto failure = parsed.find("failure");
  if (failure == parsed.end()) return RouteReceiptError::kFailure;
  if (!failure->is_null()) {
    if (!failure->is_object()) return RouteReceiptError::kFailure;
    const auto failure_class = failure->find("class");
    const auto reason = failure->find("reason");
    if (failure_class == failure->end() || !failure_class->is_string() ||
        reason == failure->end() || !reason->is_string())
      return RouteReceiptError::kFailure;
    const auto parsed_class = ParseLabel<RouteFailureClass>(
        failure_class->get<std::string>(), kFailureClassNames);
    const auto parsed_reason = ParseLabel<ValueRouteError>(
        reason->get<std::string>(), kFailureReasonNames);
    if (!parsed_class || !parsed_reason) return RouteReceiptError::kFailure;
    RouteFailure typed{*parsed_class, *parsed_reason, {}};
    const auto location = [&](const char* name, auto& target) {
      const auto field = failure->find(name);
      if (field == failure->end()) return true;
      const auto value = ParseUnsigned<std::uint32_t>(*field);
      if (!value) return false;
      using Id = typename std::decay_t<decltype(target)>::value_type;
      target = Id{*value};
      return true;
    };
    if (!location("method", typed.method) || !location("working_pc", typed.working_pc) ||
        !location("creation_method", typed.creation_method) || !location("creation_pc", typed.creation_pc))
      return RouteReceiptError::kFailure;
    if (const auto item = failure->find("printing"); item != failure->end()) {
      typed.printing=ParseTreePrintFailure(*item);if(!typed.printing)return RouteReceiptError::kFailure;
    }
    if (const auto item = failure->find("evaluation"); item != failure->end()) {
      typed.evaluation = ParseEvaluationFailure(*item); if (!typed.evaluation) return RouteReceiptError::kFailure;
    }
    if (const auto item = failure->find("tree"); item != failure->end()) {
      typed.tree = ParseTreeFailure(*item); if (!typed.tree) return RouteReceiptError::kFailure;
    }
    if (const auto item = failure->find("deferred"); item != failure->end()) {
      typed.deferred = ParseDeferredCell(*item); if (!typed.deferred) return RouteReceiptError::kFailure;
    }
    if (const auto item = failure->find("control"); item != failure->end()) {
      typed.control = ParseControlJson(*item);
      if (!typed.control) return RouteReceiptError::kFailure;
    }
    if (const auto item = failure->find("capture"); item != failure->end()) {
      if (!item->is_object()) return RouteReceiptError::kFailure;
      const auto number = [&](const char* name) -> std::optional<std::uint32_t> {
        const auto value = item->find(name);
        return value == item->end() ? std::nullopt : ParseUnsigned<std::uint32_t>(*value);
      };
      const auto ordinal = number("ordinal"), depth = number("depth"), slot = number("slot"), domain = number("requested");
      if (!ordinal || !depth || !slot || !domain || *slot > std::numeric_limits<std::uint16_t>::max())
        return RouteReceiptError::kFailure;
      const auto requested = static_cast<analysis::CaptureDomain>(*domain);
      switch (requested) {
        case analysis::CaptureDomain::kLanguage: case analysis::CaptureDomain::kDefined:
        case analysis::CaptureDomain::kGuarded: case analysis::CaptureDomain::kSourceAccess: break;
        default: return RouteReceiptError::kFailure;
      }
      if (*domain != static_cast<unsigned>(requested)) return RouteReceiptError::kFailure;
      CaptureOccurrence use{*ordinal, {{*depth}, {static_cast<std::uint16_t>(*slot)}}, requested, {}};
      if (item->contains("method")) {
        const auto method = number("method"); if (!method) return RouteReceiptError::kFailure;
        CaptureOrigin source{{*method}, {}, {}};
        if (item->contains("pc")) { const auto pc = number("pc"); if (!pc) return RouteReceiptError::kFailure; source.pc = abc::CodeOffset{*pc}; }
        if (item->contains("working_pc")) { const auto pc = number("working_pc"); if (!pc) return RouteReceiptError::kFailure; source.working_pc = abc::CodeOffset{*pc}; }
        use.source = source;
      } else if (item->contains("pc") || item->contains("working_pc")) return RouteReceiptError::kFailure;
      typed.capture = use;
    }
    if (const auto item = failure->find("call_reference"); item != failure->end()) {
      if (!item->is_object() || !item->contains("kind") || !item->contains("call"))
        return RouteReceiptError::kFailure;
      const auto kind = ParseUnsigned<std::uint32_t>((*item)["kind"]);
      const auto call = ParseUnsigned<std::uint32_t>((*item)["call"]);
      if (!kind || *kind > static_cast<unsigned>(CallReferenceError::kNotInline) || !call)
        return RouteReceiptError::kFailure;
      CallReferenceEvidence evidence{static_cast<CallReferenceError>(*kind), {*call}, {}, {}, {}, {}};
      const auto field = [&](const char* name, auto& target) {
        const auto value = item->find(name);
        if (value == item->end()) return true;
        const auto number = ParseUnsigned<std::uint32_t>(*value);
        if (!number) return false;
        using T = typename std::decay_t<decltype(target)>::value_type;
        if constexpr (std::is_same_v<T, EvaluationKind>) {
          if (*number > static_cast<unsigned>(EvaluationKind::kGuardedCellRead)) return false;
          target = static_cast<T>(*number);
        } else if constexpr (std::is_same_v<T, TemporaryReason>) {
          if (*number > static_cast<unsigned>(TemporaryReason::kEnumerationKey)) return false;
          target = static_cast<T>(*number);
        } else target = T{*number};
        return true;
      };
      if (!field("property", evidence.property) || !field("placement", evidence.placement) ||
          !field("temporary", evidence.temporary) || !field("consumer", evidence.consumer))
        return RouteReceiptError::kFailure;
      if (const auto checked = item->find("checked"); checked != item->end()) {
        evidence.checked = ParseGuardedCall(*checked);
        if (!evidence.checked || evidence.checked->call != evidence.call) return RouteReceiptError::kFailure;
      }
      typed.call_reference = evidence;
    }
    const auto pc = failure->find("pc");
    if (pc == failure->end()) return RouteReceiptError::kFailure;
    if (!pc->is_null()) {
      const auto offset = ParseUnsigned<std::uint32_t>(*pc);
      if (!offset) return RouteReceiptError::kFailure;
      typed.pc = abc::CodeOffset{*offset};
    }
    // Required and exactly typed: never defaulted (schema 5).
    const auto causes = failure->find("causes");
    const auto truncated = failure->find("causes_truncated");
    if (causes == failure->end() || !causes->is_array() ||
        causes->size() > kMaxFailureCauses || truncated == failure->end() ||
        !truncated->is_boolean())
      return RouteReceiptError::kFailure;
    for (const auto& cause : *causes) {
      if (!cause.is_object() || cause.size() != 2) return RouteReceiptError::kFailure;
      const auto domain = cause.find("domain");
      const auto value = cause.find("value");
      if (domain == cause.end() || !domain->is_string() || domain->get<std::string>().empty() ||
          value == cause.end())
        return RouteReceiptError::kFailure;
      const auto number = ParseUnsigned<std::uint32_t>(*value);
      if (!number) return RouteReceiptError::kFailure;
      typed.causes.Append({domain->get<std::string>(), *number});
    }
    typed.causes.truncated = truncated->get<bool>();
    if (typed.causes.truncated && typed.causes.count != kMaxFailureCauses)
      return RouteReceiptError::kFailure;
    receipt.failure = std::move(typed);
  }
  // A syntactically complete receipt must still describe a state the reached
  // stages could prove (T00 correction R2).
  if (const auto invalid = ValidateRouteReceipt(receipt)) return *invalid;
  return receipt;
}
}  // namespace abcd::source
