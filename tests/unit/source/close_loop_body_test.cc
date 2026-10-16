#include "../../../src/source/close_loop_body.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
constexpr auto kProfile=abc::ProfileId::kDynamic1206;
enum class Change { kNone,kExtended,kOtherDispatch,kExit,kIdentity,kCommitIdentity,kMissingEntry };
void Case(unsigned padding,Change change) {
  // Lower-layer control-region fixture. Value identity itself is supplied by
  // CloseEntries' separately tested key/SSA proof, not fabricated here as a
  // complete iterator certificate. Conditional and all links keep original IDs.
  std::vector<std::uint8_t> bytes{0xd5,static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM32),0,0,0,0,0xd5,0x61,0};
  bytes.insert(bytes.end(),padding,0xd5);
  const auto call_pc=static_cast<unsigned>(bytes.size());bytes.insert(bytes.end(),{0x29,0});
  const auto jump_pc=static_cast<unsigned>(bytes.size());bytes.insert(bytes.end(),{0x98,0,0,0,0});
  const auto exit_pc=static_cast<unsigned>(bytes.size());bytes.push_back(0x65);
  const auto handler_pc=static_cast<unsigned>(bytes.size());bytes.insert(bytes.end(),{0x61,1,0xfe,0});
  const auto other_pc=static_cast<unsigned>(bytes.size());
  if(change==Change::kOtherDispatch)bytes.insert(bytes.end(),{0x61,2,0xfe,0});
  auto body=test::Body(bytes);
  for(auto& i:body.instructions)if(i.offset.value==1 || i.offset.value==jump_pc) {
    const auto target=i.offset.value==1 || change==Change::kExit?exit_pc:0u;
    i.branch_target=abc::CodeOffset{target};i.decoded.branch_delta=std::int64_t(target)-i.offset.value;
    i.decoded.operands[0].raw_bits=static_cast<std::uint32_t>(*i.decoded.branch_delta);
  }
  abc::TryRegion wrapper;wrapper.offset={9};wrapper.length=padding+2;wrapper.handler_count=1;
  if(change==Change::kExtended)wrapper.length=exit_pc+1-9;
  abc::CatchHandler handler;handler.offset={handler_pc};handler.length=4;
  body.tries.push_back(wrapper);body.handlers.push_back(handler);
  unsigned owned=0;
  if(change==Change::kOtherDispatch) {
    auto user=wrapper;auto catch_user=handler;catch_user.offset={other_pc};
    body.tries.insert(body.tries.begin(),user);body.handlers.insert(body.handlers.begin(),catch_user);body.tries[1].first_handler=1;owned=1;
  }
  auto built=BuildExceptionFlow(body,kProfile,32);
  if(!std::holds_alternative<ExceptionFlow>(built)) {
    std::cerr<<"flow change="<<unsigned(change)<<" alternative="<<built.index()<<'\n';
    std::visit([](const auto& f){if constexpr(requires{f.kind;})std::cerr<<"kind="<<unsigned(f.kind)<<'\n';},built);Check(false);
  }
  const auto flow=std::get<ExceptionFlow>(std::move(built));
  const auto node=[&](unsigned pc,ExceptionPhase phase) {
    for(std::uint32_t n=0;n<flow.nodes.size();++n)if(flow.nodes[n].phase==phase && flow.instructions[flow.nodes[n].instruction.value].pc.value==pc)return ExceptionNodeId{n};
    Check(false);return ExceptionNodeId{};
  };
  ExceptionPlan plan;plan.layout.regions.resize(body.tries.size());plan.regions.resize(body.tries.size());
  const auto site=[&](unsigned pc){return flow.nodes[node(pc,ExceptionPhase::kBefore).value].instruction;};
  auto& region=plan.layout.regions[owned];region.body={site(9),site(change==Change::kExtended?handler_pc:jump_pc)};
  region.handler={site(handler_pc),InstructionId{static_cast<std::uint32_t>(body.instructions.size())}};
  DerivedClose close{};
  close.iteration.advance=node(0,ExceptionPhase::kBefore);close.iteration.success=node(6,ExceptionPhase::kBefore);
  close.iteration.value=change==Change::kIdentity?node(0,ExceptionPhase::kBefore):(change==Change::kCommitIdentity?node(6,ExceptionPhase::kCommit):close.iteration.success);
  close.iteration.loop_header=node(0,ExceptionPhase::kBefore);close.iteration.exit=node(exit_pc,ExceptionPhase::kBefore);
  close.iteration.exhausted={node(1,ExceptionPhase::kCommit),0};
  const auto before_call=node(call_pc,ExceptionPhase::kBefore);
  const auto catcher=node(change==Change::kOtherDispatch?other_pc:handler_pc,ExceptionPhase::kCatch);
  for(std::uint8_t e=0;e<flow.nodes[before_call.value].successor_count;++e)if(flow.nodes[before_call.value].successors[e].kind==ExceptionArcKind::kException)
    close.entries.push_back({{{before_call,e},{PendingCompletionKind::kThrow,CompletionExceptionValue{before_call},CompletionCaller{}}},before_call,catcher});
  if(change==Change::kMissingEntry)close.entries.clear();
  const auto run=[&](SemanticBudget budget={},bool reverse=false){return ProveCloseLoopBody(flow,plan,close,{owned},budget,reverse);};
  const auto result=run();Check(std::holds_alternative<CloseProtectedRange>(result));
  const auto& proof=std::get<CloseProtectedRange>(result);
  if(change==Change::kNone) {
    if(proof.missing)std::cerr<<"unexpected "<<unsigned(*proof.missing->ownership_gap)<<'\n';
    Check(!proof.missing);Check(proof.dispatch.size()==1 && proof.exits.size()==1);
    Check(proof.ordinary.size()>=padding*2+6 && !proof.transport.empty());
    Check(std::holds_alternative<CloseProtectedRange>(run({proof.peak_bytes,proof.work})));
    Check(std::get<CloseCompletionFailure>(run({proof.peak_bytes-1,proof.work})).kind==CloseCompletionError::kResourceLimit);
    Check(std::get<CloseCompletionFailure>(run({proof.peak_bytes,proof.work-1})).kind==CloseCompletionError::kWorkLimit);
    std::cout<<padding<<" bytes="<<proof.peak_bytes<<" work="<<proof.work<<'\n';
  } else {
    Check(proof.missing.has_value());
    const auto gap=(change==Change::kIdentity || change==Change::kCommitIdentity)?CloseOwnershipGap::kBodyIdentity:change==Change::kExit?CloseOwnershipGap::kBodyExit:
        change==Change::kExtended?CloseOwnershipGap::kBodyRange:CloseOwnershipGap::kBodyDispatch;
    if(proof.missing->ownership_gap!=gap)std::cerr<<"change="<<unsigned(change)<<" gap="<<unsigned(*proof.missing->ownership_gap)<<" expected="<<unsigned(gap)<<'\n';
    Check(proof.missing->ownership_gap==gap);
  }
  std::vector<std::optional<ExceptionNodeId>> before(flow.instructions.size()),commit(flow.instructions.size());
  std::vector<std::uint32_t> offsets{0};std::vector<BlockId> edges;
  for(std::uint32_t n=0;n<flow.nodes.size();++n) {
    const auto& v=flow.nodes[n];
    if(v.phase==ExceptionPhase::kBefore)before[v.instruction.value]=ExceptionNodeId{n};
    if(v.phase==ExceptionPhase::kCommit)commit[v.instruction.value]=ExceptionNodeId{n};
    for(std::uint8_t e=0;e<v.successor_count;++e)edges.push_back({v.successors[e].target.value});
    offsets.push_back(static_cast<std::uint32_t>(edges.size()));
  }
  const auto dom=std::get<DominatorTree>(BuildDominators({offsets,edges}));
  const CloseBodyIndex index{&flow,before,commit,&dom};
  const auto borrowed=std::get<CloseProtectedRange>(ProveCloseLoopBody(flow,plan,close,{owned},{},false,&index));
  Check(proof.ordinary==borrowed.ordinary && proof.transport==borrowed.transport && proof.dispatch==borrowed.dispatch && proof.exits==borrowed.exits && proof.missing==borrowed.missing);
  const auto reversed=std::get<CloseProtectedRange>(run({},true));
  Check(proof.ordinary==reversed.ordinary && proof.transport==reversed.transport && proof.dispatch==reversed.dispatch && proof.exits==reversed.exits && proof.missing==reversed.missing);
}
}
int main() {
  for(auto change:{Change::kNone,Change::kExtended,Change::kOtherDispatch,Change::kExit,Change::kIdentity,Change::kCommitIdentity,Change::kMissingEntry})Case(0,change);
  for(unsigned n:{200,201,400,800})Case(n,Change::kNone);
}
