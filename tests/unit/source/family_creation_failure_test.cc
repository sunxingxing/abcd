#include "../../../src/source/family_route_failure.h"
#include "../../../src/source/family_creation_json.h"
#include "../../../tools/record_failure_json.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) { std::cerr << "family_creation_failure_test:" << at.line() << '\n'; std::abort(); }
}
void RoundTrip(const FamilyFileFailure& failure) {
  const auto route = FamilyRouteFailure(FamilyPrepareFailure{failure});
  Check(route.family_creation == failure.family_creation);
  Check(route.causes.entries[0].domain == FailureDomain<FamilyFileError>());
  Check(route.causes.entries[1].domain == FailureDomain<FamilyCreationCheck>());
  Check(route.method == failure.family_creation->location.method);
  Check(route.pc == failure.family_creation->location.pc);
  Check(route.working_pc == failure.family_creation->location.working_pc);
  MethodRouteReceipt receipt;
  receipt.identity.file_size=4096; receipt.identity.file_hash.bytes[0]=1;
  receipt.identity.abc_version={{12,0,6,0}}; receipt.identity.profile=abc::ProfileId::kDynamic1206;
  receipt.method=abc::EntityOffset{777}; receipt.record=abc::EntityOffset{111};
  receipt.route=SourceRoute::kValueTree; receipt.stage=SourceStage::kValuesVerified;
  receipt.work_revision=0; receipt.coverage=Coverage::kUnsupported; receipt.failure=route;
  const auto serialized=SerializeRouteReceipt(receipt);
  Check(std::holds_alternative<std::string>(serialized));
  const auto parsed=ParseRouteReceipt(std::get<std::string>(serialized));
  Check(std::holds_alternative<MethodRouteReceipt>(parsed));
  const auto& actual=*std::get<MethodRouteReceipt>(parsed).failure;
  Check(actual.family_creation==route.family_creation);
  Check(actual.causes.count==route.causes.count);
  for (std::size_t i=0;i<route.causes.count;++i) {
    Check(actual.causes.entries[i].domain==route.causes.entries[i].domain);
    Check(actual.causes.entries[i].value==route.causes.entries[i].value);
  }
  nlohmann::json routed, recorded;
  tools::DescribeRecordFailure(RouteRefusal{route},routed);
  RecordProgramFailure entry{RecordProgramError::kFunction,route.pc};
  entry.family_creation=route.family_creation; entry.causes=route.causes;
  entry.method=route.method; entry.working_pc=route.working_pc;
  tools::DescribeRecordFailure(entry,recorded);
  Check(recorded["source_family_creation"]==routed["source_failure"]["family_creation"]);
  Check(recorded["source_causes"]==routed["source_failure"]["causes"]);
}
}
int main() {
  static_assert(std::is_trivially_copyable_v<FamilyCreationEvidence>);
  static_assert(static_cast<unsigned>(FamilyFileError::kCreation)==1);
  static_assert(static_cast<unsigned>(FamilyFileError::kResourceLimit)==4);
  static_assert(static_cast<unsigned>(FamilyFileError::kWorkLimit)==5);
  for(unsigned tag=0;tag<=static_cast<unsigned>(FamilyCreationCheck::kSelfInitializer);++tag) {
    FamilyFileFailure f{FamilyFileError::kCreation};
    auto& e=f.family_creation.emplace(FamilyCreationEvidence{static_cast<FamilyCreationCheck>(tag)});
    e.location.method=abc::EntityOffset{77}; e.location.operation=analysis::OpId{0};
    e.location.pc=abc::CodeOffset{0}; e.location.working_pc=abc::CodeOffset{17};
    e.root=abc::EntityOffset{55}; e.occurrence=FamilyOccurrenceId{2};e.instruction=analysis::InstructionId{0};
    e.kind=FamilyCreationKind::kClass; e.constructor=abc::PoolIndex{0}; e.literal=abc::PoolIndex{3};
    e.projected_class=analysis::OpId{0};
    e.descendant_source=TreeLocation{};e.descendant_source->method=abc::EntityOffset{55};
    e.descendant_source->pc=abc::CodeOffset{9};e.descendant_source->working_pc=abc::CodeOffset{6};
    RoundTrip(f);
    f.creation_cause=analysis::CreationError::kUnsupportedOpcode;
    auto route=FamilyRouteFailure(FamilyPrepareFailure{f});
    Check(route.causes.count==3 && route.causes.entries[2].domain==FailureDomain<analysis::CreationError>());
    RoundTrip(f);
    f.creation_cause=analysis::CreationBindingError::kUnreachable;RoundTrip(f);
    f.creation_cause=abc::MetadataError::kRead;RoundTrip(f);
    // Historical kCreation classification is authoritative; exposed child
    // causes cannot turn this diagnostic-only change into process admission.
    Check(FamilyRouteFailure(FamilyPrepareFailure{f}).failure_class==RouteFailureClass::kUnproved);
    for(auto kind:{FamilyFileError::kResourceLimit,FamilyFileError::kWorkLimit}) {
      f.cause=kind;
      Check(FamilyRouteFailure(FamilyPrepareFailure{f}).failure_class ==
          (kind==FamilyFileError::kResourceLimit?RouteFailureClass::kMemoryLimit:RouteFailureClass::kWorkLimit));
    }
    f.cause=FamilyFileError::kCreation;
    e.location.pc.reset();e.location.working_pc.reset();e.descendant_source.reset();RoundTrip(f);
    auto j=nlohmann::json::parse(FamilyCreationJson(e).dump());
    j["reason"]=static_cast<unsigned>(FamilyCreationCheck::kSelfInitializer)+1;Check(!ParseFamilyCreation(j));
    j=nlohmann::json::parse(FamilyCreationJson(e).dump());j["kind"]=99U;Check(!ParseFamilyCreation(j));
    j=nlohmann::json::parse(FamilyCreationJson(e).dump());j["constructor_pool"]=UINT64_MAX;Check(!ParseFamilyCreation(j));
    j=nlohmann::json::parse(FamilyCreationJson(e).dump());j.erase("literal_pool");Check(!ParseFamilyCreation(j));
    j=nlohmann::json::parse(FamilyCreationJson(e).dump());j["location"]["pc"]=-1;Check(!ParseFamilyCreation(j));
  }
  // Fixed-size witnesses have no input-dependent retained allocation.
  for (auto count:{200U,201U,400U,800U}) for (unsigned i=0;i<count;++i) {
    FamilyCreationEvidence e{FamilyCreationCheck::kFunctionBinding};
    e.location.method=abc::EntityOffset{i};
    const auto parsed=ParseFamilyCreation(nlohmann::json::parse(FamilyCreationJson(e).dump()));
    Check(parsed==e);
  }
  std::cout << "family_creation_failure_test: all 13 checks, both JSON paths, origin absence/zero, classes and malformed witnesses\n";
}
