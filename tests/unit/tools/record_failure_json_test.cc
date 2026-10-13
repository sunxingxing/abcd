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
  chained.method = abc::EntityOffset{91};
  chained.pc = abc::CodeOffset{73};
  chained.working_pc = abc::CodeOffset{31};
  chained.creation_method = abc::EntityOffset{13};
  chained.creation_pc = abc::CodeOffset{7};
  chained.call_reference = source::CallReferenceEvidence{source::CallReferenceError::kNotInline,
      {17}, analysis::OpId{4}, source::EvaluationKind::kRoot, source::TemporaryReason::kEffectSnapshot, {}};
  chained.control = source::ControlOwnershipEvidence{
      {17, source::ControlOriginKind::kPhase, 17, abc::CodeOffset{73}, abc::CodeOffset{31}}, 19,
      {{5, source::ControlOriginKind::kPhase, 5}, source::ControlLocation{13, source::ControlOriginKind::kTryExit, 2}},
      {{0, source::ControlOriginKind::kPhase, 0}, {}}};
  nlohmann::json with_causes;
  tools::DescribeRecordFailure(source::RouteRefusal{chained}, with_causes);
  const auto& control = with_causes["source_failure"]["control"];
  Check(control["node"]["pc"] == 73 && control["node"]["working_pc"] == 31 && control["arc"] == 19);
  Check(control["current"]["stop"]["origin"] == static_cast<unsigned>(source::ControlOriginKind::kTryExit));
  Check(!control["current"]["stop"].contains("pc") && control["previous"]["stop"].is_null());
  const auto& causes = with_causes["source_failure"]["causes"];
  Check(causes.size() == 2 && causes[1]["domain"] == "abcd::source::ValueRouteError" &&
        causes[1]["value"] == static_cast<unsigned>(source::ValueRouteError::kPrintStage));
  Check(causes[0]["domain"] == "abcd::source::RouteFailureClass");
  Check(with_causes["source_failure"]["causes_truncated"] == false);
  Check(with_causes["source_failure"]["method"] == 91 &&
      with_causes["source_failure"]["pc"] == 73 &&
      with_causes["source_failure"]["working_pc"] == 31 &&
      with_causes["source_failure"]["creation_method"] == 13 &&
      with_causes["source_failure"]["creation_pc"] == 7);
  Check(with_causes["source_failure"]["call_reference"]["kind"] == 1 &&
      with_causes["source_failure"]["call_reference"]["call"] == 17 &&
      with_causes["source_failure"]["call_reference"]["property"] == 4 &&
      with_causes["source_failure"]["call_reference"]["placement"] == 1 &&
      with_causes["source_failure"]["call_reference"]["temporary"] == 1 &&
      !with_causes["source_failure"]["call_reference"].contains("consumer"));
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
  source::RecordProgramFailure located{source::RecordProgramError::kValue, {73}};
  located.method=abc::EntityOffset{91}; located.working_pc=abc::CodeOffset{31};
  located.call_reference=chained.call_reference;
  nlohmann::json record_reference;
  tools::DescribeRecordFailure(located, record_reference);
  Check(record_reference["source_method"]==91 && record_reference["source_working_pc"]==31 &&
      record_reference["source_call_reference"]==with_causes["source_failure"]["call_reference"]);
  // Printed source is not a failure and adds no failure fields.
  nlohmann::json printed = nlohmann::json::object();
  tools::DescribeRecordFailure(source::PrintedSource{}, printed);
  Check(printed.empty());
  // Fatality is decided out of band: an observed process fault (allocation,
  // input read, input change) is fatal for every outcome, including recovered
  // success. Without one only an entry allocation failure stops the export;
  // folded codes (kRead from malformed names, kWrite from invalid literals),
  // budgets and other typed failures are per record.
  const auto fatal = [](source::RecordProgramError kind, bool process_fault) {
    return tools::RecordFailureIsFatal(source::RecordProgramFailure{kind, {}}, process_fault);
  };
  using Error = source::RecordProgramError;
  for (const auto kind : {Error::kRecordBudget, Error::kOutputLimit, Error::kEntry,
                          Error::kResourceLimit, Error::kExport, Error::kFileIndex,
                          Error::kSourcePath, Error::kRead, Error::kWrite, Error::kFileChanged}) {
    Check(!fatal(kind, false));
    Check(fatal(kind, true));
  }
  Check(fatal(Error::kAllocationFailure, false) && fatal(Error::kAllocationFailure, true));
  const source::RouteRefusal memory{{source::RouteFailureClass::kMemoryLimit,
      source::ValueRouteError::kTreeStage, {}}};
  Check(!tools::RecordFailureIsFatal(memory, false) && tools::RecordFailureIsFatal(memory, true));
  Check(!tools::RecordFailureIsFatal(source::PrintedSource{}, false));
  Check(tools::RecordFailureIsFatal(source::PrintedSource{}, true));
  std::cout << "record_failure_json_test: ok\n";
  return EXIT_SUCCESS;
}
