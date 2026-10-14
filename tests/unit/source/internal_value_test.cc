#include "../../../src/source/family_route_failure.h"
#include "../../../src/source/internal_value_json.h"
#include "../../../tools/record_failure_json.h"
#include "../../../src/source/cell_initialization.h"
#include "abcd/analysis/value_import.h"
#include "analysis/ssa_fixture.h"
#include <bit>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
constexpr auto W(abc::Opcode x) { return static_cast<std::uint8_t>(x); }
abc::MethodBody Loop(std::size_t count, unsigned variant=0, bool store=true, bool reset=false, bool capture=false) {
  std::vector<std::uint8_t> code;
  const auto emit=[&](std::initializer_list<std::uint8_t> bytes) { code.insert(code.end(),bytes); };
  const auto jump=[&](abc::Opcode op) { const auto at=code.size(); emit({W(op),0,0,0,0}); return at; };
  const auto bind=[&](std::size_t at,std::size_t target) {
    const auto delta=static_cast<std::uint32_t>(static_cast<std::int32_t>(target)-static_cast<std::int32_t>(at));
    for(unsigned j=0;j<4;++j) code[at+j+1]=static_cast<std::uint8_t>(delta>>(8*j));
  };
  emit({W(abc::Opcode::kNEWLEXENV_IMM8),1,W(abc::Opcode::kLDAI_IMM32),7,0,0,0});
  if(store)emit({0x3d,0});
  if(capture)emit({W(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8),0,0,0,0});
  const auto initial=jump(abc::Opcode::kJMP_IMM32);
  const auto head=code.size();bind(initial,head);
  emit({0x60,0});const auto exit=jump(abc::Opcode::kJEQZ_IMM32);
  if(reset)emit({W(abc::Opcode::kNEWLEXENV_IMM8),1});
  for(std::size_t i=0;i<count;++i) {
    emit({0x3c,0});
    if(variant==1)emit({0x61,1,0x60,1});
    if(variant==2){const auto split=jump(abc::Opcode::kJMP_IMM32);bind(split,code.size());}
    emit({0x61,static_cast<std::uint8_t>(variant==1?2:1)});
  }
  emit({W(abc::Opcode::kLDFALSE),0x61,0});
  const auto back=jump(abc::Opcode::kJMP_IMM32);bind(back,head);bind(exit,code.size());
  emit({0x3c,0,W(abc::Opcode::kRETURN)});
  return test::Body(code);
}
void Diagnostic() {
  static_assert(std::is_trivially_copyable_v<InternalValueEvidence>);
  for(auto kinds:{ValueKinds{},ValueKinds::Hole(),ValueKinds::Environment(),ValueKinds::OtherInternal(),
      ValueKinds::PropertyEnumerator(),ValueKinds::Unknown(),ValueKinds::Language()}) {
    InternalValueEvidence e{InternalReason(kinds),SsaValueId{0},{0},0,InstructionValue{{0}},kinds};
    for(unsigned origin=0;origin<3;++origin) {
      if(origin==0)e.producer=MethodEntry{};if(origin==1)e.producer=InstructionValue{{0}};if(origin==2)e.producer=PhiValue{{0}};
      auto j=InternalValueJson(e);auto decoded=ParseInternalValue(nlohmann::json::parse(j.dump()));
      Check(decoded && InternalValueJson(*decoded)==j);
      auto bad=j;bad["reason"]=99U;Check(!ParseInternalValue(bad));
      bad=j;bad["read_ordinal"]=-1;Check(!ParseInternalValue(bad));
      bad=j;bad["domains"].push_back(99U);Check(!ParseInternalValue(bad));
      SemanticProofFailure proof{SemanticProofError::kInternalValue,{0},0,0,e};
      auto route=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{FunctionFileFailure{
          FunctionInputFailure{proof,abc::EntityOffset{42},abc::CodeOffset{17}}}}});
      Check(route.internal_value && route.pc==abc::CodeOffset{0} && route.working_pc==abc::CodeOffset{17});
      Check(route.failure_class==RouteFailureClass::kUnproved && route.causes.count==2);
      MethodRouteReceipt receipt;
      receipt.identity.file_size=4096;receipt.identity.file_hash.bytes[0]=1;
      receipt.identity.abc_version={{12,0,6,0}};receipt.identity.profile=abc::ProfileId::kDynamic1206;
      receipt.method=abc::EntityOffset{777};receipt.record=abc::EntityOffset{111};
      receipt.route=SourceRoute::kValueTree;receipt.stage=SourceStage::kValuesVerified;
      receipt.work_revision=0;receipt.coverage=Coverage::kUnsupported;receipt.failure=route;
      auto encoded=SerializeRouteReceipt(receipt);
      Check(std::holds_alternative<std::string>(encoded));
      const auto wire=std::get<std::string>(encoded);
      auto decoded_receipt=ParseRouteReceipt(wire);
      if (!std::holds_alternative<MethodRouteReceipt>(decoded_receipt)) std::cerr<<wire<<"\n";
      Check(std::holds_alternative<MethodRouteReceipt>(decoded_receipt));
      const auto round=std::get<MethodRouteReceipt>(decoded_receipt);
      Check(InternalValueJson(*round.failure->internal_value)==j);
      nlohmann::json manifest,entry,omitted;
      tools::DescribeRecordFailure(RouteRefusal{route},manifest);
      RecordProgramFailure f{RecordProgramError::kBody,abc::CodeOffset{0}};
      f.causes=route.causes;f.internal_value=e;
      tools::DescribeRecordFailure(f,entry);
      Check(manifest["source_failure"]["internal_value"]==entry["source_internal_value"]);
      PrintedSource partial;partial.incomplete=true;
      partial.omitted_members.push_back({{1},{2},{42},SemanticProofError::kInternalValue,{0},abc::CodeOffset{17},{},{},{},{},e});
      tools::DescribeRecordFailure(std::move(partial),omitted);
      Check(omitted["source_omitted_members"][0]["internal_value"]==j);
      Check(omitted["source_omitted_members"][0]["causes"]==entry["source_causes"]);
    }
  }
  for(auto reason:{InternalValueReason::kCyclicCellInitialization,InternalValueReason::kCellInitialization,InternalValueReason::kCellPreservation}) {
    InternalValueEvidence e{reason,{}, {0}, {}, {}, {}, LocalCell{{0},{0}},InstructionId{0},{}};
    auto j=InternalValueJson(e);Check(ParseInternalValue(nlohmann::json::parse(j.dump())).has_value());
    j["value"]=0U;Check(!ParseInternalValue(j));
  }
  const auto body=test::Body({W(abc::Opcode::kLDHOLE),W(abc::Opcode::kRETURN)});
  const auto method=std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,3));
  const auto f=std::get<SemanticProofFailure>(VerifySemanticSource(body,method,3,0));
  Check(f.internal_value && f.internal_value->reason==InternalValueReason::kHole);
  Check(f.internal_value->observer.value==1 && std::get<InstructionValue>(*f.internal_value->producer).instruction.value==0);
  InternalValueEvidence missing{InternalValueReason::kMissingFact,SsaValueId{0},{0},0,MethodEntry{},{}};
  Check(ParseInternalValue(nlohmann::json::parse(InternalValueJson(missing).dump())).has_value());
  const auto limited=std::get<SemanticProofFailure>(VerifySemanticSource(body,method,3,0,{0,0}));
  Check(limited.kind==SemanticProofError::kResourceLimit && !limited.internal_value);
  const auto resource=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{FunctionFileFailure{
      FunctionInputFailure{limited}}}});
  Check(resource.failure_class==RouteFailureClass::kMemoryLimit);
  const auto guarded=test::Body({0x3c,0,0xfe,0x09,0,0,W(abc::Opcode::kRETURN)});
  const auto guarded_method=std::get<SemanticMethod>(BuildSemanticMethod(guarded,abc::ProfileId::kDynamic1206,3));
  const auto rejected=std::get<SemanticProofFailure>(VerifySemanticSource(guarded,guarded_method,3,0));
  Check(rejected.pc.value==2 && rejected.internal_value && rejected.internal_value->reason==InternalValueReason::kMixed);

}
void Cyclic(std::size_t n,unsigned variant) {
  const auto body=Loop(n,variant);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
  const auto method=std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,3));
  const auto prove=[&](SemanticBudget budget=SemanticBudget{}) {return VerifySemanticSource(body,method,3,0,budget);};
  const auto proof=std::get<SemanticProof>(prove());
  Check(!proof.locals.initializations.empty());
  const auto graph=std::get<ValueProgram>(ImportNormalValues(body,method,proof,frame));
  Check(!VerifyImportedValues(graph,method,proof,frame));
  const auto environments=std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto cells=std::get<CellPlan>(BuildCellPlan(graph,environments));
  const auto facts=std::get<CellInitializations>(BuildCellInitializations(graph,environments,cells));
  Check(std::holds_alternative<std::uint64_t>(VerifyCellInitializations(graph,environments,cells,ClosureWrites::kUnknown,facts)));
  auto bad=facts;
  for(auto& fact:bad.reads)if(auto* store=std::get_if<StoredInitialization>(&fact)){store->store=OpId{0};break;}
  Check(std::holds_alternative<CellInitializationError>(VerifyCellInitializations(graph,environments,cells,ClosureWrites::kUnknown,bad)));
  std::size_t lo=0,hi=64*1024*1024;
  while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<SemanticProof>(prove({mid,kDefaultSemanticWork})))hi=mid;else lo=mid+1;}
  Check(std::holds_alternative<SemanticProof>(prove({lo,kDefaultSemanticWork})));
  Check(std::get<SemanticProofFailure>(prove({lo-1,kDefaultSemanticWork})).kind==SemanticProofError::kResourceLimit);
  std::uint64_t low=0,high=kDefaultSemanticWork;
  while(low<high){const auto mid=low+(high-low)/2;if(std::holds_alternative<SemanticProof>(prove({64*1024*1024,mid})))high=mid;else low=mid+1;}
  Check(std::holds_alternative<SemanticProof>(prove({64*1024*1024,low})));
  Check(std::get<SemanticProofFailure>(prove({64*1024*1024,low-1})).kind==SemanticProofError::kResourceLimit);
  std::cout<<n<<' '<<variant<<" bytes="<<lo<<" work="<<low<<'\n';
}
}
int main(){
  Diagnostic();
  for(auto n:{200U,201U,400U,800U})for(unsigned variant=0;variant<3;++variant)Cyclic(n,variant);
  const auto captured=Loop(1,0,true,false,true);
  const auto captured_method=std::get<SemanticMethod>(BuildSemanticMethod(captured,abc::ProfileId::kDynamic1206,3));
  const auto capture_failure=std::get<SemanticProofFailure>(VerifySemanticSource(captured,captured_method,3,0));
  Check(capture_failure.internal_value && capture_failure.internal_value->reason==InternalValueReason::kCellPreservation);
  Check(std::holds_alternative<SemanticProof>(VerifySemanticSource(captured,captured_method,3,0,{},
      CapturedValues::kReject,{},ClosureWrites::kLanguage)));
  // A literal Hole reset is still an illegal store input, including after a
  // successful allocation/store/read dominance proof. Never bless all writes
  // because one initialization dominates a load.
  const auto hole_store=test::Body({W(abc::Opcode::kNEWLEXENV_IMM8),1,
      W(abc::Opcode::kLDHOLE),0x3d,0,W(abc::Opcode::kRETURNUNDEFINED)});
  const auto hole_method=std::get<SemanticMethod>(BuildSemanticMethod(hole_store,abc::ProfileId::kDynamic1206,3));
  const auto hole_failure=std::get<SemanticProofFailure>(VerifySemanticSource(hole_store,hole_method,3,0));
  Check(hole_failure.internal_value && hole_failure.internal_value->reason==InternalValueReason::kHole);
  for(const auto body:{Loop(1,0,false),Loop(1,0,true,true)}) {
    const auto method=std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,3));
    const auto result=VerifySemanticSource(body,method,3,0);
    Check(std::holds_alternative<SemanticProofFailure>(result));
  }
}
