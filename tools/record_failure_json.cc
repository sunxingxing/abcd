#include "record_failure_json.h"

#include <type_traits>

namespace abcd::tools {
namespace {
nlohmann::json DescribeCauses(const source::FailureCauseChain& chain) {
  nlohmann::json causes = nlohmann::json::array();
  for (const auto cause : chain.View())
    causes.push_back({{"domain", cause.domain}, {"value", cause.value}});
  return causes;
}
}  // namespace

void DescribeRecordFailure(const source::RecordSourceResult& result,
                           nlohmann::json& record) {
  std::visit([&](const auto& failure) {
    using T = std::decay_t<decltype(failure)>;
    if constexpr (std::is_same_v<T, source::RouteRefusal>) {
      record["source_failure_domain"] = "route_refusal";
      record["source_failure"] = {
          {"class", source::RouteFailureClassName(failure.failure.failure_class)},
          {"reason", source::ValueRouteErrorName(failure.failure.reason)}};
      if (failure.failure.pc) record["source_failure"]["pc"] = failure.failure.pc->value;
      record["source_failure"]["causes"] = DescribeCauses(failure.failure.causes);
      record["source_failure"]["causes_truncated"] = failure.failure.causes.truncated;
    } else if constexpr (std::is_same_v<T, source::RecordProgramFailure>) {
      record["source_failure_domain"] = "record_program";
      record["source_error"] = static_cast<unsigned>(failure.kind);
      record["source_pc"] = failure.pc.value;
      record["source_causes"] = DescribeCauses(failure.causes);
      record["source_causes_truncated"] = failure.causes.truncated;
    }
  }, result);
}

bool RecordFailureIsFatal(const source::RecordSourceResult& result,
                          bool allocation_failed) {
  if (const auto* refusal = std::get_if<source::RouteRefusal>(&result))
    return allocation_failed &&
           refusal->failure.failure_class == source::RouteFailureClass::kMemoryLimit;
  const auto* failure = std::get_if<source::RecordProgramFailure>(&result);
  if (!failure) return false;
  using Error = source::RecordProgramError;
  switch (failure->kind) {
    case Error::kRead:
    case Error::kWrite:
    case Error::kFileChanged:
    case Error::kAllocationFailure:
      return true;
    case Error::kResourceLimit:
      return allocation_failed;
    default:
      return false;
  }
}
}  // namespace abcd::tools
