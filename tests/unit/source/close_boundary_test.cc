#include "../../../src/source/close_boundary.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
constexpr auto kProfile=abc::ProfileId::kDynamic1206;
constexpr std::uint64_t kFrame=32;
enum class Change { kNone,kDeadCopy,kDeadCopyChain,kLiveCopyChain,kLiveOut,kAccLiveOut,kKilled,kExternalEntry,kUserTry,kForgedUserTry,kDropExit };
void Case(unsigned padding,Change change) {
  // Entry nop; private guard write; private code; port; continuation. These
  // are raw boundary unit tests, not claimed iterator acquisition evidence.
  std::vector<std::uint8_t> bytes{0xd5,0x02,0x61,0};
  bytes.insert(bytes.end(),padding+1,0xd5);
  const auto port_pc=static_cast<unsigned>(bytes.size());
  if(change==Change::kKilled){bytes.push_back(0x02);bytes.push_back(0x61);bytes.push_back(0);}
  if(change==Change::kDeadCopy || change==Change::kDeadCopyChain || change==Change::kLiveCopyChain || change==Change::kLiveOut || change==Change::kKilled){bytes.push_back(0x60);bytes.push_back(0);}
  if(change==Change::kDeadCopyChain || change==Change::kLiveCopyChain)bytes.insert(bytes.end(),{0x61,1,0x60,1});
  bytes.push_back((change==Change::kAccLiveOut || change==Change::kLiveOut || change==Change::kLiveCopyChain)?0x64:0x65);
  const auto handler_pc=static_cast<unsigned>(bytes.size());
  if((change==Change::kUserTry || change==Change::kForgedUserTry))bytes.insert(bytes.end(),{0x61,1,0x65});
  auto body=test::Body(bytes);
  if((change==Change::kUserTry || change==Change::kForgedUserTry)) {
    abc::TryRegion user;user.offset={1};user.length=1;user.handler_count=1;
    abc::CatchHandler handler;handler.offset={handler_pc};handler.length=3;
    body.tries.push_back(user);body.handlers.push_back(handler);
  }
  if(change==Change::kExternalEntry) {
    // A live bypass goes into the middle of the private region.
    std::vector<std::uint8_t> extra{0x4f,5,0x02,0x61,0,0xd5,0xd5,0x65};
    body=test::Body(extra);
  }
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,kFrame));
  IdfQuota work{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,kFrame,work));
  const auto node=[&](unsigned pc,ExceptionPhase phase) {
    for(std::uint32_t n=0;n<flow.nodes.size();++n)if(flow.nodes[n].phase==phase && flow.instructions[flow.nodes[n].instruction.value].pc.value==pc)return ExceptionNodeId{n};
    Check(false);return ExceptionNodeId{};
  };
  const unsigned first_pc=change==Change::kExternalEntry?2:1;
  const unsigned last_pc=change==Change::kExternalEntry?6:port_pc-1;
  const auto first=node(first_pc,ExceptionPhase::kBefore);
  const auto last=node(last_pc,ExceptionPhase::kCommit);
  CompletionEdge entry{node(0,ExceptionPhase::kCommit),0};
  if(change==Change::kExternalEntry) {
    const auto& source=flow.nodes[entry.source.value];
    for(std::uint8_t e=0;e<source.successor_count;++e)if(source.successors[e].target==first)entry.successor=e;
  }
  DerivedClose close{};
  close.entries.push_back({{entry,{PendingCompletionKind::kBreak,{},flow.nodes[last.value].successors[0].target}},entry.source});
  close.exits.push_back(CompletionEdge{last,0});
  CloseCompletionRegion region{};
  // Derive the actual reachability from the entry to the exit cut, rather
  // than a PC-ordered window (the production builder supplies the same cut).
  std::vector<bool> seen(flow.nodes.size());std::vector<ExceptionNodeId> todo{first};seen[first.value]=true;
  for(std::size_t i=0;i<todo.size();++i)if(todo[i]!=last)for(std::uint8_t e=0;e<flow.nodes[todo[i].value].successor_count;++e) {
    const auto next=flow.nodes[todo[i].value].successors[e].target;
    if(!seen[next.value]){seen[next.value]=true;todo.push_back(next);}
  }
  std::sort(todo.begin(),todo.end());for(const auto n:todo)region.members.push_back({n,CloseMemberRole::kScalar});
  const std::array incoming{close.entries[0].entry};
  const auto relation=std::get<CompletionRelation>(AnalyzeCompletionRelation(body,kProfile,kFrame,flow,ssa,incoming,close.exits));
  region.outcomes.emplace();for(const auto& leaving:relation.leaving)
    region.outcomes->leaving.push_back({leaving.entry,leaving.exit,leaving.pending,{}});
  if(change==Change::kDropExit)close.exits.clear();
  ExceptionPlan plan;IteratorProtocolScan scan;
  if((change==Change::kUserTry || change==Change::kForgedUserTry)) {
    plan.layout=std::get<ExceptionRegions>(BuildExceptionRegions(body,flow));
    plan.regions.resize(plan.layout.regions.size());
    if(change==Change::kForgedUserTry) {
      IteratorRegionCandidate forged{};forged.region={0};forged.role=IteratorRegionRole::kReturnProtection;
      scan.regions.push_back(forged);
      region.members.front().role=CloseMemberRole::kReturnRead; // forged semantic role is insufficient too
    }
  }
  const auto run=[&](SemanticBudget budget={},bool reverse=false){return ProveCloseBoundary(body,kProfile,flow,kFrame,plan,scan,close,region,budget,reverse);};
  const auto result=run();
  if(change==Change::kDropExit){Check(std::holds_alternative<CloseCompletionFailure>(result));return;}
  Check(std::holds_alternative<CloseBoundaryProof>(result));
  const auto& proof=std::get<CloseBoundaryProof>(result);
  if(change==Change::kLiveCopyChain || change==Change::kLiveOut || change==Change::kAccLiveOut || change==Change::kUserTry || change==Change::kForgedUserTry || change==Change::kExternalEntry) {
    Check(proof.missing.has_value());
    if(change==Change::kLiveCopyChain || change==Change::kLiveOut || change==Change::kAccLiveOut)Check(proof.missing->ownership_gap==CloseOwnershipGap::kLiveOut);
    if((change==Change::kUserTry || change==Change::kForgedUserTry))Check(proof.missing->ownership_gap==CloseOwnershipGap::kUserTry);
    if(change==Change::kExternalEntry)Check(proof.missing->kind==CloseCompletionError::kExternalEntry);
  } else Check(!proof.missing);
  Check(proof.entry_coverage==(change==Change::kExternalEntry?CloseProofState::kMissing:CloseProofState::kVerified));
  Check(proof.port_coverage==CloseProofState::kVerified);
  Check(proof.private_state==((change==Change::kLiveCopyChain || change==Change::kLiveOut || change==Change::kAccLiveOut)?CloseProofState::kMissing:CloseProofState::kVerified));
  const auto reversed=std::get<CloseBoundaryProof>(run({},true));
  Check(proof.ports==reversed.ports && proof.unreachable_entries==reversed.unreachable_entries && proof.private_slots==reversed.private_slots && proof.missing==reversed.missing);
  Check(proof.private_slots==std::vector<ClosePrivateSlot>{{},{abc::RegisterId{0}}});
  Check(!proof.ports.empty());
  if(change==Change::kNone) {
    Check(std::holds_alternative<CloseBoundaryProof>(run({proof.peak_bytes,proof.work})));
    Check(std::get<CloseCompletionFailure>(run({proof.peak_bytes-1,proof.work})).kind==CloseCompletionError::kResourceLimit);
    Check(std::get<CloseCompletionFailure>(run({proof.peak_bytes,proof.work-1})).kind==CloseCompletionError::kWorkLimit);
    std::cout<<padding<<" bytes="<<proof.peak_bytes<<" work="<<proof.work<<'\n';
  }
}
void MissingExceptionEdge() {
  auto body=test::Body({0x29,0,0x65,0x61,0,0x65});
  abc::TryRegion region;region.offset={0};region.length=2;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={3};handler.length=3;
  body.tries.push_back(region);body.handlers.push_back(handler);
  auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,kFrame));
  bool changed=false;
  for(auto& node:flow.nodes)if(node.phase==ExceptionPhase::kBefore && flow.instructions[node.instruction.value].pc.value==0) {
    Check(node.successor_count==2);--node.successor_count;changed=true;
  }
  Check(changed);
  const auto result=ProveCloseBoundary(body,kProfile,flow,kFrame,{}, {}, {}, {}, {},false);
  Check(std::get<CloseCompletionFailure>(result).kind==CloseCompletionError::kCorrespondence);
}
}
int main() {
  MissingExceptionEdge();
  for(const auto change:{Change::kNone,Change::kDeadCopy,Change::kDeadCopyChain,Change::kLiveCopyChain,Change::kLiveOut,Change::kAccLiveOut,Change::kKilled,Change::kExternalEntry,Change::kUserTry,Change::kForgedUserTry,Change::kDropExit})Case(0,change);
  for(unsigned n:{200,201,400,800}){Case(n,Change::kNone);Case(n,Change::kDeadCopyChain);Case(n,Change::kLiveCopyChain);}
}
