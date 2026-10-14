#include "../../../tools/record_failure_json.h"
#include "abcd/analysis/exception_inputs.h"
#include "abcd/analysis/semantic_validation.h"
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
  RecordSourceResult result{std::move(source)};
  nlohmann::json manifest{{"source_status",4}}, cli{{"source_complete",false}};
  tools::DescribeRecordFailure(result, manifest);
  tools::DescribeRecordFailure(result, cli);
  Check(manifest["source_omitted_members"] == cli["source_omitted_members"]);
  const auto decoded = nlohmann::json::parse(manifest.dump());
  Check(decoded == manifest);
  const auto& items = decoded["source_omitted_members"];
  Check(items.size()==2 && items[0]["pc"]==0 && !items[0].contains("working_pc"));
  Check(items[0]["method"]==2 && !items[0].contains("creation_method"));
  Check(items[0]["causes"]==nlohmann::json::array({{{"domain","abcd::analysis::SemanticProofError"},{"value",3}}}));
  Check(items[1]["method"]==4 && items[1]["member"]==3 && items[1]["constructor"]==1);
  Check(items[1]["working_pc"]==41 && items[1]["pc"]==44 && items[1]["creation_pc"]==0);
  Check(items[1]["exception_node"]==0 && items[1]["value"]==5);
  Check(items[1]["causes"]==nlohmann::json::array({{{"domain","abcd::analysis::ExceptionInputError"},{"value",1}}}));
  Check(!manifest.contains("source_failure"));
  nlohmann::json complete;
  tools::DescribeRecordFailure(PrintedSource{},complete);
  Check(!complete.contains("source_omitted_members"));
}
