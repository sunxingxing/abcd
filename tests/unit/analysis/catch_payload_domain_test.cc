#include "abcd/analysis/catch_payload_domain.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <algorithm>
using namespace abcd;
using namespace abcd::analysis;
using test::Check;
namespace {
constexpr auto kProfile=abc::ProfileId::kDynamic1206;
void Case(std::vector<std::uint8_t> bytes,bool pinned,bool premise,unsigned padding=0,bool require_phi=false) {
  // Handler is a real exception entry, with no source-language input facts.
  bytes.insert(bytes.begin(),padding,0xd5);
  const auto handler=static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(),{0x61,2,0x65});
  auto body=test::Body(bytes);
  body.tries.push_back({{padding},handler-padding,0,1});
  body.handlers.push_back({{}, {handler},3});
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,8));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,8,quota));
  if(require_phi)Check(std::any_of(ssa.Values().begin(),ssa.Values().end(),[](const auto& v){return std::holds_alternative<PhiValue>(v.origin);}));
  for(auto policy:{ExternalExceptionPolicy::kPinnedRuntime,ExternalExceptionPolicy::kNoHoleV1}) {
    const bool expect=policy==ExternalExceptionPolicy::kPinnedRuntime?pinned:premise;
    const auto run=[&](SemanticBudget budget) {
      RawEventArena arena(budget);
      const auto facts=DeriveCatchPayloadDomains(flow,ssa,policy,arena);
      Check(facts.size()==1 && facts[0].sources>0);
      Check((facts[0].domain==CatchPayloadDomain::kNonHole)==expect);
      RawEventArena checked({});VerifyCatchPayloadDomains(flow,ssa,policy,facts,checked);
      for(unsigned mutation=0;mutation<4;++mutation) {
        std::vector<CatchPayloadFact> bad(facts.begin(),facts.end());
        switch(mutation) {
          case 0:bad[0].domain=expect?CatchPayloadDomain::kHolePossible:CatchPayloadDomain::kNonHole;break;
          case 1:++bad[0].entry.value;break;
          case 2:++bad[0].sources;break;
          case 3:bad.clear();break;
        }
        bool refused=false;RawEventArena verify({});
        try{VerifyCatchPayloadDomains(flow,ssa,policy,bad,verify);}
        catch(const RawEventFailure& f){refused=f.kind==RawEventError::kCorrespondence;}
        Check(refused);
      }
      return std::pair{arena.Peak(),arena.Work()};
    };
    const auto [peak,work]=run({});
    Check(run({peak,work})==std::pair{peak,work});
    bool bytes_failed=false,work_failed=false;
    try{(void)run({peak-1,work});}catch(const std::bad_alloc&){bytes_failed=true;}
    catch(const RawEventFailure& f){bytes_failed=f.kind==RawEventError::kResourceLimit;}
    try{(void)run({peak,work-1});}catch(const RawEventFailure& f){work_failed=f.kind==RawEventError::kWorkLimit;}
    Check(bytes_failed && work_failed);
    if(padding)std::cout<<"padding="<<padding<<" bytes="<<peak<<" work="<<work<<'\n';
  }
}
void Nested(bool local_hole) {
  std::vector<std::uint8_t> bytes=local_hole?std::vector<std::uint8_t>{0x70,0xfe,0}:std::vector<std::uint8_t>{0x29,0};
  const auto protected_size=static_cast<std::uint32_t>(bytes.size());bytes.push_back(0x65);
  const auto first=static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(),{0x61,1,0x60,1,0xfe,0,0x65,0x61,2,0x65});
  auto body=test::Body(bytes);
  body.tries={{{0},protected_size,0,1},{{first+4},2,1,1}};
  body.handlers={{{},{first},7},{{},{first+7},3}};
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,8));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,8,quota));
  for(auto policy:{ExternalExceptionPolicy::kPinnedRuntime,ExternalExceptionPolicy::kNoHoleV1}) {
    RawEventArena arena({});const auto facts=DeriveCatchPayloadDomains(flow,ssa,policy,arena);
    Check(facts.size()==2);
    for(const auto& f:facts)Check((f.domain==CatchPayloadDomain::kNonHole)==(!local_hole && policy==ExternalExceptionPolicy::kNoHoleV1));
    RawEventArena verify({});VerifyCatchPayloadDomains(flow,ssa,policy,facts,verify);
  }
}

}
int main() {
  Nested(false);Nested(true);
  Case({0x29,0,0x65},false,true); // Call's arbitrary external exception.
  Case({0x70,0xfe,0,0x65},false,false); // Explicit local Hole is NEVER covered.
  Case({0x02,0xfe,0,0x65},true,true); // Local true is non-Hole without a premise.
  Case({0xfe,5,0,0x65},true,true); // ObjectCheck constructs TypeError.
  Case({0x29,0,0xfe,0,0x65},false,false); // A locally thrown CALL result is not an external exception.
  Case({0x29,0,0x70,0xfe,0,0x65},false,false); // Mixed call and explicit Hole.
  Case({0x29,0,0x02,0xfe,0,0x65},false,true); // Mixed external and proven local.
  Case({0x70,0x61,1,0x60,1,0xfe,0,0x65},false,false); // Copied Hole.
  Case({0x02,0x61,1,0x60,1,0xfe,0,0x65},true,true); // Copied language constant.
  // Distinct SSA paths merge at the local THROW; no path is omitted.
  Case({0x60,0,0x4f,7,0x02,0x61,1,0x4d,5,0x70,0x61,1,0x60,1,0xfe,0,0x65},false,false,0,true);
  Case({0x60,0,0x4f,7,0x02,0x61,1,0x4d,5,0x03,0x61,1,0x60,1,0xfe,0,0x65},true,true,0,true);
  // A seeded loop phi with a self-copy still needs an induction proof. The
  // least proof solution is unknown, rather than assuming a cyclic fact.
  Case({0x02,0x61,1,0x60,0,0x4f,8,0x60,1,0x61,1,0x4d,0xf8,0x60,1,0xfe,0,0x65},false,false,0,true);
  for(auto n:{200u,201u,400u,800u})Case({0x29,0,0x65},false,true,n);
}
