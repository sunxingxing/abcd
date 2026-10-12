#include "record_failure_json.h"

#include <type_traits>

namespace abcd::tools {
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
    } else if constexpr (std::is_same_v<T, source::RecordProgramFailure>) {
      record["source_failure_domain"] = "record_program";
      record["source_error"] = static_cast<unsigned>(failure.kind);
      record["source_pc"] = failure.pc.value;
    }
  }, result);
}
}  // namespace abcd::tools
