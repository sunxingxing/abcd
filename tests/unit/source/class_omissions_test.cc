#include "../../../tools/record_failure_json.h"
#include "abcd/analysis/exception_inputs.h"
#include "abcd/analysis/semantic_validation.h"
#include "../../../src/source/exception_control.h"
#include "../../../src/source/phased_body.h"
#include "../../../src/source/family_route_failure.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
namespace {
void Check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) { std::cerr << "class_omissions_test:" << at.line() << '\n'; std::abort(); }
}
}
int main() {
  using namespace source;
  static_assert(std::is_trivially_copyable_v<OmittedClassMember>);
  PrintedSource source;
  source.incomplete = true;
  source.omitted_members.push_back({{1}, {2}, {2}, analysis::SemanticProofError::kInternalValue,
      {0}, {}, {}, {}, {}, {}});
  source.omitted_members.push_back({{1}, {3}, {4}, analysis::ExceptionInputError::kInternalValue,
      {44}, abc::CodeOffset{41}, abc::EntityOffset{3}, abc::CodeOffset{0},
      analysis::ExceptionNodeId{0}, analysis::SsaValueId{5}});
  auto progressed = source.omitted_members.back();
  progressed.control = ExceptionControlError::kMultipleExits;
  progressed.pc = {91}; progressed.working_pc = abc::CodeOffset{90};
  progressed.exception_region = ExceptionRegionEvidence{{2}, abc::CodeOffset{91}, abc::CodeOffset{112}, {90}, {111}, {}};
  progressed.node.reset(); progressed.value.reset();
  source.omitted_members.push_back(progressed);
  auto extended=progressed;
  auto extra=std::make_shared<FailureCauseChain>();
  AppendFailureCause(*extra,ForOfIterationError::kAdvance);
  extended.additional_causes=OmissionCauseId{0};
  TreeLocation location;location.method=abc::EntityOffset{4};
  location.pc=abc::CodeOffset{123};location.working_pc=abc::CodeOffset{120};
  extended.protocol_location=location;
  source.omission_causes.push_back(std::move(extra));
  source.omitted_members.push_back(extended);
  auto refined=extended;
  refined.later_causes=OmissionCauseId{1};refined.later_location=TreeLocation{.method=abc::EntityOffset{4},.pc=abc::CodeOffset{220},.working_pc=abc::CodeOffset{180}};
  auto later=std::make_shared<FailureCauseChain>();
  AppendFailureCause(*later,FunctionTreeError::kExpression);AppendFailureCause(*later,CallReferenceError::kNotInline);
  source.omission_causes.push_back(std::move(later));source.omitted_members.push_back(refined);
  analysis::ExceptionInputFailure input{analysis::ExceptionInputError::kInternalValue,{17}};input.node=analysis::ExceptionNodeId{1};input.value=analysis::SsaValueId{2};
  FamilyTreeFailure later_tree{{0},FunctionTreeFailure{FunctionTreeError::kExpression}};
  later_tree.method=abc::EntityOffset{9};
  Check(FamilyRouteFailure(later_tree).method==FamilyRouteFailure(FamilyPrepareFailure{later_tree}).method);
  Check(FamilyRouteFailure(later_tree).causes.View().size()==FamilyRouteFailure(FamilyPrepareFailure{later_tree}).causes.View().size());
  PhasedInputFailure legacy{input};Check(IsOmittablePhasedRefusal(legacy));
  legacy.cause.kind=analysis::ExceptionInputError::kMismatchedAnalysis;Check(!IsOmittablePhasedRefusal(legacy));
  PhasedControlFailure control{PhasedMethodError::kControl,ExceptionControlRegionFailure{ExceptionControlError::kMultipleExits,{0},{}},{4}};
  control.input_gap=PhasedInputFailure{input};Check(!IsOmittablePhasedRefusal(control));
  std::get<ExceptionControlRegionFailure>(control.cause).exception_region=ExceptionRegionEvidence{};
  Check(IsOmittablePhasedRefusal(control));control.kind=PhasedMethodError::kResourceLimit;Check(!IsOmittablePhasedRefusal(control));
  RecordSourceResult result{std::move(source)};
  nlohmann::json manifest{{"source_status",4}}, cli{{"source_complete",false}};
  tools::DescribeRecordFailure(result, manifest);
  tools::DescribeRecordFailure(result, cli);
  Check(manifest["source_omitted_members"] == cli["source_omitted_members"]);
  const auto decoded = nlohmann::json::parse(manifest.dump());
  Check(decoded == manifest);
  const auto& items = decoded["source_omitted_members"];
  Check(items.size()==5 && items[0]["pc"]==0 && !items[0].contains("working_pc"));
  Check(items[0]["method"]==2 && !items[0].contains("creation_method"));
  Check(items[0]["causes"]==nlohmann::json::array({{{"domain","abcd::analysis::SemanticProofError"},{"value",3}}}));
  Check(items[1]["method"]==4 && items[1]["member"]==3 && items[1]["constructor"]==1);
  Check(items[1]["working_pc"]==41 && items[1]["pc"]==44 && items[1]["creation_pc"]==0);
  Check(items[1]["exception_node"]==0 && items[1]["value"]==5);
  Check(items[1]["causes"]==nlohmann::json::array({{{"domain","abcd::analysis::ExceptionInputError"},{"value",1}}}));
  Check(items[2]["causes"] == nlohmann::json::array({
      {{"domain", "abcd::source::PhasedMethodError"}, {"value", 6}},
      {{"domain", "abcd::source::ExceptionControlError"}, {"value", 4}}}));
  Check(items[2]["pc"] == 91 && items[2]["working_pc"] == 90);
  Check(items[2]["exception_region"]["handler_pc"] == 112);
  Check(!items[2].contains("exception_node"));
  Check(items[3]["causes"].back()==nlohmann::json{{"domain","abcd::source::ForOfIterationError"},{"value",3}});
  Check(items[3]["protocol_location"]["pc"]==123);
  Check(items[3]["protocol_location"]["working_pc"]==120);
  Check(items[4]["pc"]==91 && items[4]["later_failure"]["location"]["pc"]==220);
  Check(items[4]["later_failure"]["causes"].back()["domain"]=="abcd::source::CallReferenceError");
  Check(!manifest.contains("source_failure"));
  nlohmann::json complete;
  tools::DescribeRecordFailure(PrintedSource{},complete);
  Check(!complete.contains("source_omitted_members"));
}
