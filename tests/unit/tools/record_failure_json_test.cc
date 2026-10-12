#include "record_failure_json.h"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main() {
  using namespace abcd;
  // A route refusal names its domain, class, reason and optional pc.
  nlohmann::json refused;
  tools::DescribeRecordFailure(source::RouteRefusal{{source::RouteFailureClass::kUnproved,
      source::ValueRouteError::kTreeStage, abc::CodeOffset{13}}}, refused);
  Check(refused["source_failure_domain"] == "route_refusal");
  Check(refused["source_failure"]["class"] == "unproved");
  Check(refused["source_failure"]["reason"] == "tree_stage");
  Check(refused["source_failure"]["pc"] == 13);
  Check(!refused.contains("source_error") && !refused.contains("shared_failure_variant"));
  // Typed causes serialize outermost first; the last entry is the leaf.
  source::RouteFailure chained{source::RouteFailureClass::kUnsupported,
      source::ValueRouteError::kTreeStage, {}};
  source::AppendFailureCause(chained.causes, source::RouteFailureClass::kUnproved);
  source::AppendFailureCause(chained.causes, source::ValueRouteError::kPrintStage);
  nlohmann::json with_causes;
  tools::DescribeRecordFailure(source::RouteRefusal{chained}, with_causes);
  const auto& causes = with_causes["source_failure"]["causes"];
  Check(causes.size() == 2 && causes[1]["domain"] == "abcd::source::ValueRouteError" &&
        causes[1]["value"] == static_cast<unsigned>(source::ValueRouteError::kPrintStage));
  Check(causes[0]["domain"] == "abcd::source::RouteFailureClass");
  Check(with_causes["source_failure"]["causes_truncated"] == false);
  source::RouteFailure full{source::RouteFailureClass::kUnproved, source::ValueRouteError::kTreeStage, {}};
  for (std::size_t i = 0; i <= source::kMaxFailureCauses; ++i)
    source::AppendFailureCause(full.causes, source::ValueRouteError::kTreeStage);
  Check(full.causes.count == source::kMaxFailureCauses && full.causes.truncated);
  nlohmann::json unlocated;
  tools::DescribeRecordFailure(source::RouteRefusal{{source::RouteFailureClass::kWorkLimit,
      source::ValueRouteError::kContextStage, {}}}, unlocated);
  Check(!unlocated["source_failure"].contains("pc"));
  // A record-level failure carries its typed error and pc, never route fields.
  nlohmann::json budget;
  tools::DescribeRecordFailure(source::RecordProgramFailure{
      source::RecordProgramError::kResourceLimit, abc::CodeOffset{7}}, budget);
  Check(budget["source_failure_domain"] == "record_program");
  Check(budget["source_error"] == static_cast<unsigned>(source::RecordProgramError::kResourceLimit));
  Check(budget["source_pc"] == 7 && !budget.contains("source_failure"));
  Check(budget["source_causes"].empty() && budget["source_causes_truncated"] == false);
  // Printed source is not a failure and adds no failure fields.
  nlohmann::json printed = nlohmann::json::object();
  tools::DescribeRecordFailure(source::PrintedSource{}, printed);
  Check(printed.empty());
  // Fatality: input I/O/change and entry allocation failure always stop the
  // export; resource limits only when a real allocation failure was observed.
  const auto fatal = [](source::RecordProgramError kind, bool allocation_failed) {
    return tools::RecordFailureIsFatal(source::RecordProgramFailure{kind, {}}, allocation_failed);
  };
  using Error = source::RecordProgramError;
  for (const bool observed : {false, true}) {
    Check(!fatal(Error::kRecordBudget, observed));
    Check(!fatal(Error::kOutputLimit, observed));
    Check(!fatal(Error::kEntry, observed));
    Check(fatal(Error::kAllocationFailure, observed));
    Check(fatal(Error::kRead, observed) && fatal(Error::kWrite, observed) &&
          fatal(Error::kFileChanged, observed));
    Check(fatal(Error::kResourceLimit, observed) == observed);
    Check(tools::RecordFailureIsFatal(source::RouteRefusal{{source::RouteFailureClass::kMemoryLimit,
        source::ValueRouteError::kTreeStage, {}}}, observed) == observed);
    Check(!tools::RecordFailureIsFatal(source::RouteRefusal{{source::RouteFailureClass::kUnproved,
        source::ValueRouteError::kTreeStage, {}}}, observed));
    Check(!tools::RecordFailureIsFatal(source::PrintedSource{}, observed));
  }
  std::cout << "record_failure_json_test: ok\n";
  return EXIT_SUCCESS;
}
