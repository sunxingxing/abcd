#include "abcd/analysis/await_catch_flow.h"
#include "abcd/analysis/await_catch.h"
#include "abcd/analysis/exception_dispatch.h"
#include "../../support/analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
namespace {
constexpr auto kProfile = abc::ProfileId::kDynamic1206;
constexpr std::uint32_t kFrame = 128;
struct Fixture { abc::MethodBody body; std::vector<AwaitRegion> regions; abc::CodeOffset handler, completion; };
Fixture Make(std::span<const std::uint8_t> handler, unsigned padding = 0, unsigned awaits = 1) {
  // The existing pinned AwaitRegion adapter owns this protocol. The new proof
  // never recognizes this byte sequence itself; all handler graphs below vary.
  std::vector<std::uint8_t> bytes{0xae,0x61,0,0x60,3,0xc4,0,0xc3,0,0x60,0,
    0xbf,0x61,1,0x60,0,0xc0,0x61,2,0x62,1,0,0,0,0x0f,0,2,0x4f,6,
    0x60,1,0xfe,0,0x60,1,0xcd,0,0x64};
  const std::vector<std::uint8_t> protocol(bytes.begin()+3,bytes.end()-3);
  for(unsigned i=1;i<awaits;++i)bytes.insert(bytes.end()-3,protocol.begin(),protocol.end());
  const abc::CodeOffset caught{static_cast<std::uint32_t>(bytes.size())};
  bytes.insert(bytes.end(),handler.begin(),handler.end());
  bytes.insert(bytes.end(),padding,static_cast<std::uint8_t>(abc::Opcode::kNOP));
  bytes.insert(bytes.end(),{0,0xcd,0,0x64});
  const abc::CodeOffset completion{static_cast<std::uint32_t>(bytes.size())};
  bytes.insert(bytes.end(),{0x61,4,0xce,0,0x64});
  auto body=test::Body(bytes);
  abc::TryRegion user;user.offset={3};user.length=caught.value-3;user.handler_count=1;
  abc::CatchHandler h;h.offset=caught;h.length=completion.value-caught.value;
  abc::TryRegion outer;outer.offset={3};outer.length=completion.value-3;outer.first_handler=1;outer.handler_count=1;
  abc::CatchHandler end;end.offset=completion;end.length=5;
  body.tries={user,outer};body.handlers={h,end};
  const auto found=FindAwaitRegions(body,kProfile,kFrame,SemanticBudget{}.bytes);
  test::Check(std::holds_alternative<std::vector<AwaitRegion>>(found));
  return {std::move(body),std::get<std::vector<AwaitRegion>>(found),caught,completion};
}
AwaitCatchFlowProof Run(const Fixture& f, SemanticBudget budget={}) {
  auto selected=BuildExceptionDispatch(f.body,kProfile);
  if(auto* d=std::get_if<DispatchFailure>(&selected))std::cerr<<"dispatch "<<static_cast<unsigned>(d->kind)<<" @"<<d->offset.value<<'\n';
  auto result=ProveAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,budget);
  if(auto* e=std::get_if<CatchFlowFailure>(&result)) {std::cerr<<"flow failure "<<static_cast<unsigned>(e->kind)<<'\n';test::Check(false);}
  auto proof=std::get<AwaitCatchFlowProof>(std::move(result));
  test::Check(std::holds_alternative<CatchFlowCost>(VerifyAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,proof,budget)));
  return proof;
}
void Reject(const Fixture& f,const AwaitCatchFlowProof& proof) {
  test::Check(std::holds_alternative<CatchFlowFailure>(VerifyAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,proof)));
}
void Cases() {
  // Reads of caught payload remain real, but are not reads of result v1/mode v2.
  const std::array<std::uint8_t,6> covered{0x61,4,0x60,4,0x61,1};
  auto f=Make(covered);auto proof=Run(f);test::Check(proof.queries.size()==2);
  test::Check(proof.queries[0].outcome==CatchFlowOutcome::kCovered);
  test::Check(proof.queries[1].outcome==CatchFlowOutcome::kTerminal);
  test::Check(proof.queries[0].cover==abc::CodeOffset{f.handler.value+4});
  // Read-before-write wins, including an arithmetic instruction's acc commit.
  const std::array<std::uint8_t,5> read{0x61,4,0x60,1,0x70};
  auto blockSplit=Make(std::array<std::uint8_t,8>{0x61,4,0x4d,2,0x60,4,0x61,1});
  test::Check(Run(blockSplit).queries[0].outcome==proof.queries[0].outcome);
  auto observed=Make(read);auto seen=Run(observed);
  test::Check(seen.queries[0].outcome==CatchFlowOutcome::kObservedRead);
  auto self=Make(std::array<std::uint8_t,5>{0x61,4,0x45,1,1});
  test::Check(Run(self).queries[0].outcome==CatchFlowOutcome::kObservedRead);
  // Nontermination is not an observation. A closed jump cycle is admissible.
  const std::array<std::uint8_t,4> cycle{0x61,4,0x4d,0};
  auto loop=Make(cycle);auto looping=Run(loop);
  test::Check(looping.queries[0].outcome==CatchFlowOutcome::kUnobserved);
  // Two normal successors cover the same incoming register independently.
  const std::array<std::uint8_t,15> branch{0x61,4,0x70,0x4f,8,0x60,3,0x61,1,0x4d,6,0x60,4,0x61,1};
  auto split=Make(branch);auto branching=Run(split);
  test::Check(branching.queries[0].outcome==CatchFlowOutcome::kCovered);
  // A may-throw call before an overwrite: exception carries pre-Commit state.
  const std::array<std::uint8_t,6> throwing{0x61,4,0x29,0,0x61,1};
  auto exception=Make(throwing);auto ex=Run(exception);
  test::Check(ex.queries[0].outcome==CatchFlowOutcome::kCoveredAndTerminal);
  // Route that exception back to a genuine incoming-value read.
  abc::TryRegion inner;inner.offset={exception.handler.value+2};inner.length=2;inner.handler_count=1;inner.first_handler=1;
  abc::CatchHandler back;back.offset={33};back.length=2;
  exception.body.tries.insert(exception.body.tries.begin()+1,inner);exception.body.handlers.insert(exception.body.handlers.begin()+1,back);exception.body.tries.back().first_handler=2;
  auto leaking=Run(exception);test::Check(leaking.queries[0].outcome==CatchFlowOutcome::kObservedRead);
  // Unowned resume is not treated as a terminal/no-op even when unused.
  const std::array<std::uint8_t,3> unknown{0x61,4,0xbf};
  auto unsupported=Make(unknown);test::Check(Run(unsupported).queries[0].outcome==CatchFlowOutcome::kUnknownEffect);
  test::Check(std::holds_alternative<AwaitCatchProof>(ProveAwaitCatchInputsWithFlow(
      f.body,f.regions,kProfile,kFrame,f.completion,proof)));
  test::Check(std::holds_alternative<AwaitCatchFailure>(ProveAwaitCatchInputsWithFlow(
      observed.body,observed.regions,kProfile,kFrame,observed.completion,seen)));
  auto mutated=proof;mutated.queries[0].query.slot={2};Reject(f,mutated);
  mutated=proof;mutated.queries[0].query.definition={0};Reject(f,mutated);
  mutated=proof;mutated.queries[0].outcome=CatchFlowOutcome::kTerminal;Reject(f,mutated);
  mutated=proof;mutated.queries[0].cover={0};Reject(f,mutated);
  test::Check(std::holds_alternative<AwaitCatchFailure>(ProveAwaitCatchInputsWithFlow(
      f.body,f.regions,kProfile,kFrame,f.completion,mutated)));
  mutated=proof;mutated.states.pop_back();Reject(f,mutated);
  mutated=proof;mutated.states.push_back(mutated.states.back());Reject(f,mutated);
  // Rename registers and split blocks without changing the semantic question.
  auto copies=Make(std::array<std::uint8_t,8>{0x61,4,0x60,4,0x61,5,0x60,5});
  test::Check(Run(copies).queries[0].outcome==CatchFlowOutcome::kTerminal);
  auto wrong=f;wrong.regions[0].result={2};
  test::Check(std::holds_alternative<CatchFlowFailure>(ProveAwaitCatchFlow(wrong.body,wrong.regions,kProfile,kFrame,wrong.completion)));
  // Changing the original done/branch edge invalidates a sealed certificate.
  auto changed=split;for(auto& site:changed.body.instructions)
    if(site.offset==abc::CodeOffset{split.handler.value+3})site.branch_target=abc::CodeOffset{33};
  Reject(changed,branching);
  // Changing dispatch (missing exception edge) cannot be hidden in the proof.
  auto dispatch=exception;dispatch.body.tries.erase(dispatch.body.tries.begin()+1);dispatch.body.handlers.erase(dispatch.body.handlers.begin()+1);dispatch.body.tries.back().first_handler=1;Reject(dispatch,leaking);
}
void Dense() {
  // Many distinct queried slots share one handler. The register/event product
  // is dense: pre-paid sparse admission must refuse, never report unobserved.
  auto f=Make(std::array<std::uint8_t,2>{0x61,100},800,32);
  for(unsigned r=0;r<f.regions.size();++r) {
    const auto region=f.regions[r];
    for(auto& site:f.body.instructions) if(site.offset>=region.await&&site.offset<=region.continuation)
      for(unsigned j=0;j<site.decoded.operand_count;++j) {
        auto& operand=site.decoded.operands[j];
        if(operand.kind!=abc::OperandKind::kRegister)continue;
        if(operand.raw_bits==1)operand.raw_bits=2*r+1;
        else if(operand.raw_bits==2)operand.raw_bits=2*r+2;
      }
  }
  auto found=FindAwaitRegions(f.body,kProfile,kFrame,SemanticBudget{}.bytes);
  test::Check(std::holds_alternative<std::vector<AwaitRegion>>(found));
  f.regions=std::get<std::vector<AwaitRegion>>(std::move(found));
  const auto result=ProveAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion);
  test::Check(std::holds_alternative<CatchFlowFailure>(result));
  const auto reason=std::get<CatchFlowFailure>(result).kind;
  test::Check(reason==CatchFlowError::kResourceLimit || reason==CatchFlowError::kWorkLimit);
}
void Scale() {
  const std::array<std::uint8_t,2> handler{0x61,4};
  for(unsigned n:{200U,201U,400U,800U}) {
    auto f=Make(handler,n);auto proof=Run(f);
    test::Check(proof.states.size()<=8*f.body.instructions.size()*4);
    std::size_t lo=0,hi=SemanticBudget{}.bytes;
    while(lo<hi){auto mid=lo+(hi-lo)/2;
      if(std::holds_alternative<AwaitCatchFlowProof>(ProveAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,{mid})))hi=mid;else lo=mid+1;}
    test::Check(lo>0);test::Check(std::holds_alternative<AwaitCatchFlowProof>(ProveAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,{lo})));
    const auto limited=std::get<CatchFlowFailure>(ProveAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,{lo-1}));
    test::Check(limited.kind==CatchFlowError::kResourceLimit);
    std::size_t vlo=0,vhi=SemanticBudget{}.bytes;
    while(vlo<vhi){auto mid=vlo+(vhi-vlo)/2;
      if(std::holds_alternative<CatchFlowCost>(VerifyAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,proof,{mid})))vhi=mid;else vlo=mid+1;}
    test::Check(vlo>0&&std::holds_alternative<CatchFlowCost>(VerifyAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,proof,{vlo})));
    test::Check(std::get<CatchFlowFailure>(VerifyAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,proof,{vlo-1})).kind==CatchFlowError::kResourceLimit);
    auto many=Make(handler,0,n);const auto shared=Run(many);test::Check(shared.queries.size()==2*n);
    auto work=SemanticBudget{};work.work=1;
    test::Check(std::get<CatchFlowFailure>(ProveAwaitCatchFlow(f.body,f.regions,kProfile,kFrame,f.completion,work)).kind==CatchFlowError::kWorkLimit);
    std::cout<<"catch flow n="<<n<<" states="<<proof.states.size()<<" work="<<proof.work<<" exact bytes="<<lo<<" verify bytes="<<vlo<<" shared-query work="<<shared.work<<'\n';
  }
}
}
int main(){Cases();Dense();Scale();}
