#include "abcd/source/source_route.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace abcd::source {
namespace {
using Json = nlohmann::json;

constexpr std::optional<abc::CodeOffset> ParsePc(const Json& value) {
  if (value.is_null()) return {};
  if (!value.is_number_unsigned()) return {};
  return abc::CodeOffset{value.get<std::uint32_t>()};
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
}  // namespace

RouteAdmission AdmitLegacyWriter(RoutePolicy policy) {
  // T00: the value-tree route has no stages implemented, so requiring it is
  // a typed refusal at admission. When later tasks add stages this remains
  // the single decision point: legacy stays permitted only under
  // LegacyAllowed, and a refused attempt never falls back to this writer.
  if (policy == RoutePolicy::kLegacyAllowed)
    return RouteAdmission{true, {}};
  return RouteAdmission{false,
                        RouteFailure{RouteFailureClass::kUnsupported,
                                     ValueRouteError::kRouteUnavailable, {}}};
}

bool RouteObligationsClosed(
    RoutePolicy policy, std::span<const ChildRouteObligation> obligations) {
  const auto required_route = policy == RoutePolicy::kRequireValueTree
                                  ? SourceRoute::kValueTree
                                  : SourceRoute::kLegacy;
  for (const auto& obligation : obligations) {
    if (!obligation.receipt) return false;
    const auto& receipt = *obligation.receipt;
    if (receipt.method != obligation.method) return false;
    if (receipt.route != required_route) return false;
    if (receipt.coverage != Coverage::kComplete) return false;
    if (receipt.failure) return false;
  }
  return true;
}

std::string SerializeRouteReceipt(const MethodRouteReceipt& receipt) {
  Json failure = nullptr;
  if (receipt.failure) {
    failure = Json{{"class", RouteFailureClassName(
                                 receipt.failure->failure_class)},
                   {"reason", ValueRouteErrorName(receipt.failure->reason)},
                   {"pc", receipt.failure->pc
                              ? Json(receipt.failure->pc->value)
                              : Json(nullptr)}};
  }
  return Json{
      {"schema", kRouteReceiptSchema},
      {"method", receipt.method ? Json(receipt.method->value) : Json(nullptr)},
      {"record", receipt.record ? Json(receipt.record->value) : Json(nullptr)},
      {"route", SourceRouteName(receipt.route)},
      {"stage", receipt.stage ? Json(SourceStageName(*receipt.stage))
                              : Json(nullptr)},
      {"coverage", CoverageName(receipt.coverage)},
      {"legacy_writer_used", receipt.legacy_writer_used},
      {"failure", std::move(failure)}}
      .dump();
}

std::variant<MethodRouteReceipt, RouteReceiptError> ParseRouteReceipt(
    std::string_view text) {
  Json parsed = Json::parse(text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object())
    return RouteReceiptError::kSyntax;
  const auto schema = parsed.find("schema");
  if (schema == parsed.end() || !schema->is_number_unsigned() ||
      schema->get<unsigned>() != kRouteReceiptSchema)
    return RouteReceiptError::kSchema;
  MethodRouteReceipt receipt;
  const auto method = parsed.find("method");
  const auto record = parsed.find("record");
  if (method == parsed.end() || record == parsed.end())
    return RouteReceiptError::kIdentity;
  if (!method->is_null()) {
    if (!method->is_number_unsigned()) return RouteReceiptError::kIdentity;
    receipt.method = abc::EntityOffset{method->get<std::uint32_t>()};
  }
  if (!record->is_null()) {
    if (!record->is_number_unsigned()) return RouteReceiptError::kIdentity;
    receipt.record = abc::EntityOffset{record->get<std::uint32_t>()};
  }
  if (!receipt.method && !receipt.record)
    return RouteReceiptError::kIdentity;
  static constexpr std::string_view kRouteNames[]{"legacy", "value_tree"};
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
    static constexpr std::string_view kStageNames[]{
        "decoded", "input_verified", "values_imported", "values_verified",
        "control_frozen", "bindings_frozen", "tree_verified", "printed",
        "published"};
    const auto value = ParseLabel<SourceStage>(stage->get<std::string>(),
                                                kStageNames);
    if (!value) return RouteReceiptError::kStage;
    receipt.stage = *value;
  }
  static constexpr std::string_view kCoverageNames[]{"complete", "incomplete",
                                                    "unsupported"};
  const auto coverage = parsed.find("coverage");
  if (coverage == parsed.end() || !coverage->is_string())
    return RouteReceiptError::kCoverage;
  if (const auto value = ParseLabel<Coverage>(coverage->get<std::string>(),
                                              kCoverageNames))
    receipt.coverage = *value;
  else
    return RouteReceiptError::kCoverage;
  const auto legacy = parsed.find("legacy_writer_used");
  if (legacy != parsed.end() && legacy->is_boolean())
    receipt.legacy_writer_used = legacy->get<bool>();
  const auto failure = parsed.find("failure");
  if (failure != parsed.end() && !failure->is_null()) {
    if (!failure->is_object()) return RouteReceiptError::kFailure;
    static constexpr std::string_view kClassNames[]{
        "unsupported", "unproved", "invalid_input", "invariant_violation",
        "work_limit", "memory_limit", "output_failure"};
    static constexpr std::string_view kReasonNames[]{"route_unavailable"};
    const auto failure_class = failure->find("class");
    const auto reason = failure->find("reason");
    if (failure_class == failure->end() || !failure_class->is_string() ||
        reason == failure->end() || !reason->is_string())
      return RouteReceiptError::kFailure;
    const auto parsed_class =
        ParseLabel<RouteFailureClass>(failure_class->get<std::string>(),
                                      kClassNames);
    const auto parsed_reason =
        ParseLabel<ValueRouteError>(reason->get<std::string>(),
                                    kReasonNames);
    if (!parsed_class || !parsed_reason) return RouteReceiptError::kFailure;
    RouteFailure typed{*parsed_class, *parsed_reason, {}};
    const auto pc = failure->find("pc");
    if (pc != failure->end()) {
      const auto offset = ParsePc(*pc);
      if (!pc->is_null() && !offset) return RouteReceiptError::kFailure;
      typed.pc = offset;
    }
    receipt.failure = std::move(typed);
  }
  return receipt;
}
}  // namespace abcd::source
