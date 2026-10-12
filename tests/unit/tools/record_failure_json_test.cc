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
  // Printed source is not a failure and adds no failure fields.
  nlohmann::json printed = nlohmann::json::object();
  tools::DescribeRecordFailure(source::PrintedSource{}, printed);
  Check(printed.empty());
  // Fatality: only proven record-owned budget/output failures are isolated.
  const auto fatal = [](source::RecordProgramError kind) {
    return tools::RecordFailureIsFatal(source::RecordProgramFailure{kind, {}});
  };
  Check(!fatal(source::RecordProgramError::kRecordBudget));
  Check(!fatal(source::RecordProgramError::kOutputLimit));
  Check(!fatal(source::RecordProgramError::kEntry));
  Check(fatal(source::RecordProgramError::kResourceLimit));
  Check(fatal(source::RecordProgramError::kAllocationFailure));
  Check(fatal(source::RecordProgramError::kRead) && fatal(source::RecordProgramError::kWrite) &&
        fatal(source::RecordProgramError::kFileChanged));
  Check(!tools::RecordFailureIsFatal(source::RouteRefusal{{source::RouteFailureClass::kUnproved,
      source::ValueRouteError::kTreeStage, {}}}));
  Check(!tools::RecordFailureIsFatal(source::PrintedSource{}));
  std::cout << "record_failure_json_test: ok\n";
  return EXIT_SUCCESS;
}
