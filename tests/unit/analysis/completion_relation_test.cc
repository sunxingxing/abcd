#include "abcd/analysis/completion_relation.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
using test::Check;
namespace {
constexpr auto kProfile = abc::ProfileId::kDynamic1206;
constexpr std::uint64_t kFrame = 32;
struct Fixture {
  abc::MethodBody body;
  ExceptionFlow flow;
  ExceptionSsa ssa;
  explicit Fixture(abc::MethodBody b) : body(std::move(b)),
      flow(std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,kFrame))), ssa(Build(flow)) {}
  static ExceptionSsa Build(const ExceptionFlow& flow) {
    IdfQuota quota{kDefaultSemanticWork};
    return std::get<ExceptionSsa>(ExceptionSsa::Build(flow,kFrame,quota));
  }
  ExceptionNodeId Node(unsigned pc, ExceptionPhase phase) const {
    for (std::uint32_t n=0;n<flow.nodes.size();++n)
      if (flow.nodes[n].phase==phase && flow.instructions[flow.nodes[n].instruction.value].pc.value==pc) return {n};
    Check(false); return {};
  }
  CompletionEdge Edge(unsigned pc, unsigned next) const {
    auto from=Node(pc,ExceptionPhase::kCommit), to=Node(next,ExceptionPhase::kBefore);
    for (std::uint8_t s=0;s<flow.nodes[from.value].successor_count;++s)
      if (flow.nodes[from.value].successors[s].target==to) return {from,s};
    Check(false); return {};
  }
  SsaValueId Read(unsigned pc, unsigned ordinal=0) const { return ssa.Reads(Node(pc,ExceptionPhase::kBefore))[ordinal].value; }
};
CompletionRelation Run(const Fixture& f, std::span<const CompletionEntry> entries,
    std::span<const CompletionExit> exits, CompletionRelationBudget budget={}) {
  auto result=AnalyzeCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,entries,exits,budget);
  if (auto* e=std::get_if<CompletionRelationFailure>(&result)) {
    std::cerr<<"relation "<<unsigned(e->kind)<<" node "<<(e->node?e->node->value:UINT32_MAX)<<'\n';Check(false);
  }
  auto relation=std::get<CompletionRelation>(std::move(result));
  Check(std::holds_alternative<CompletionRelationCost>(VerifyCompletionRelation(
      f.body,kProfile,kFrame,f.flow,f.ssa,entries,exits,relation,budget)));
  return relation;
}
void Correlation() {
  // The paths assign the same two cells. SSA phis must select BOTH values
  // using the actual predecessor, not form kind/payload/target Cartesian sets.
  Fixture f(test::Body({0x60,0,0x4f,12,0x60,1,0x61,3,0x02,0x61,4,0x4d,10,
                        0x00,0x60,2,0x61,3,0x03,0x61,4,
                        0x60,4,0x4f,5,0x60,3,0x64,0x60,3,0xfe,0}));
  const auto a=f.Read(4), b=f.Read(14);
  const std::array entries{
    CompletionEntry{f.Edge(2,4),{PendingCompletionKind::kReturn,a,CompletionCaller{}}},
    CompletionEntry{f.Edge(2,14),{PendingCompletionKind::kThrow,b,CompletionCaller{}}}};
  const std::array exits{CompletionExit{CompletionCallerExit{f.Node(27,ExceptionPhase::kCommit)}},
                         CompletionExit{CompletionCallerExit{f.Node(30,ExceptionPhase::kBefore)}}};
  const auto r=Run(f,entries,exits);
  Check(r.arrivals.size()==2 && r.leaving.size()==2);
  Check(r.leaving[0].pending.kind==PendingCompletionKind::kReturn && r.leaving[0].pending.payload==CompletionPayload{a});
  Check(r.leaving[1].pending.kind==PendingCompletionKind::kThrow && r.leaving[1].pending.payload==CompletionPayload{b});
  // A phi at the entry destination is not available on the source edge.
  for(std::uint32_t v=0;v<f.ssa.Values().size();++v)
    if(const auto* phi=std::get_if<PhiValue>(&f.ssa.Values()[v].origin)) {
      auto inputs=entries;inputs[0].binding=SsaValueId{v};
      const auto invalid=AnalyzeCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,inputs,exits);
      Check(std::get<CompletionRelationFailure>(invalid).kind==CompletionRelationError::kPayload);
      break;
    }
  auto bad=r;bad.leaving[0].pending.payload=b;
  Check(std::holds_alternative<CompletionRelationFailure>(VerifyCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,entries,exits,bad)));
  bad=r;bad.leaving[0].pending.target=f.Node(21,ExceptionPhase::kBefore);
  Check(std::holds_alternative<CompletionRelationFailure>(VerifyCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,entries,exits,bad)));
  bad=r;bad.arrivals.pop_back();
  Check(std::holds_alternative<CompletionRelationFailure>(VerifyCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,entries,exits,bad)));
}

std::vector<CompletionExit> Callers(const Fixture& f) {
  std::vector<CompletionExit> exits;
  for(std::uint32_t n=0;n<f.flow.nodes.size();++n) {
    const auto& node=f.flow.nodes[n];const auto& op=f.flow.instructions[node.instruction.value].operation;
    if((node.phase==ExceptionPhase::kBefore && node.exception && !node.exception->target) ||
       (node.phase==ExceptionPhase::kCommit && std::holds_alternative<ReturnOperation>(op)))
      exits.push_back(CompletionCallerExit{{n}});
  }
  return exits;
}
abc::MethodBody CloseBody(bool keep_original) {
  // Pending value v0 saved in v3; a close call can fail into an actual Catch.
  auto body=keep_original ? test::Body({0xd5,0x60,0,0x61,3,0x29,0,0x60,3,0xfe,0,
                                  0x61,4,0x60,3,0xfe,0}) :
                       test::Body({0xd5,0x60,0,0x61,3,0x29,0,0x60,3,0x64,
                                  0x61,4,0x60,4,0xfe,0});
  abc::TryRegion region;region.offset={5};region.length=2;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={keep_original?11u:10u};handler.length=6;
  body.tries={region};body.handlers={handler};return body;
}
void ClosePriority() {
  for(const bool original_throw:{false,true}) {
    Fixture f(CloseBody(original_throw));const auto value=f.Read(1);
    const std::array entries{CompletionEntry{f.Edge(0,1),
      {original_throw?PendingCompletionKind::kThrow:PendingCompletionKind::kReturn,value,CompletionCaller{}}}};
    const auto exits=Callers(f);const auto r=Run(f,entries,exits);
    Check(r.leaving.size()==2);
    for(const auto& out:r.leaving) {
      if(original_throw) Check(out.pending.kind==PendingCompletionKind::kThrow && out.pending.payload==CompletionPayload{value});
      else if(out.pending.kind==PendingCompletionKind::kThrow)
        Check(out.pending.payload==CompletionPayload{CompletionExceptionValue{f.Node(5,ExceptionPhase::kBefore)}});
      else Check(out.pending.kind==PendingCompletionKind::kReturn && out.pending.payload==CompletionPayload{value});
    }
  }
}
void Nested() {
  auto body=test::Body({0xd5,0x60,0,0x61,3,0x29,0,0x60,3,0x64,
                       0x61,4,0x60,1,0x29,0,0x60,4,0xfe,0,
                       0x61,5,0x60,4,0xfe,0});
  abc::TryRegion inner;inner.offset={5};inner.length=2;inner.handler_count=1;
  abc::TryRegion outer;outer.offset={14};outer.length=2;outer.handler_count=1;outer.first_handler=1;
  abc::CatchHandler h1;h1.offset={10};h1.length=10;
  abc::CatchHandler h2;h2.offset={20};h2.length=6;
  body.tries={inner,outer};body.handlers={h1,h2};Fixture f(std::move(body));
  const auto original=f.Read(1);
  const std::array entries{CompletionEntry{f.Edge(0,1),{PendingCompletionKind::kReturn,original,CompletionCaller{}}}};
  const auto exits=Callers(f);const auto r=Run(f,entries,exits);
  Check(r.leaving.size()==3);
  const CompletionPayload inner_error=CompletionExceptionValue{f.Node(5,ExceptionPhase::kBefore)};
  for(const auto& out:r.leaving)
    if(out.pending.kind==PendingCompletionKind::kThrow) Check(out.pending.payload==inner_error);
  const CompletionEdge into_handler{f.Node(5,ExceptionPhase::kBefore),1};
  auto inner_exits=exits;inner_exits.push_back(into_handler);
  const auto first=Run(f,entries,inner_exits);
  const auto found=std::find_if(first.leaving.begin(),first.leaving.end(),[&](const auto& e){return e.exit==CompletionExit{into_handler};});
  Check(found!=first.leaving.end() && found->pending.kind==PendingCompletionKind::kThrow);
  Check(found->pending.target==CompletionTarget{f.Node(10,ExceptionPhase::kCatch)});
  // The next close is seeded with CURRENT Throw(innerError), never Return(V).
  const std::array outer_entries{CompletionEntry{into_handler,found->pending}};
  const auto second=Run(f,outer_entries,exits);
  Check(second.leaving.size()==2);
  for(const auto& out:second.leaving) Check(out.pending.kind==PendingCompletionKind::kThrow && out.pending.payload==inner_error);
}
void Targets() {
  Fixture f(test::Body({0xd5,0xd5,0xd5,0x65}));
  const std::array entries{
    CompletionEntry{f.Edge(0,1),{PendingCompletionKind::kBreak,{},f.Node(2,ExceptionPhase::kBefore)}},
    CompletionEntry{f.Edge(0,1),{PendingCompletionKind::kContinue,{},f.Node(0,ExceptionPhase::kBefore)}}};
  const std::array exits{CompletionExit{f.Edge(1,2)}};
  const auto r=Run(f,entries,exits);Check(r.leaving.size()==2);
  Check(r.leaving[0].pending==entries[0].pending && r.leaving[1].pending==entries[1].pending);
  auto bad=r;bad.leaving[0].pending.target=entries[1].pending.target;
  Check(std::holds_alternative<CompletionRelationFailure>(VerifyCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,entries,exits,bad)));
}
void Variants() {
  auto body=CloseBody(false);
  for(auto& instruction:body.instructions)
    for(unsigned i=0;i<instruction.decoded.operand_count;++i)
      if(instruction.decoded.operands[i].kind==abc::OperandKind::kRegister) instruction.decoded.operands[i].raw_bits+=8;
  Fixture renamed(std::move(body));
  const auto check=[](const Fixture& f,unsigned call) {
    const auto source=f.Read(1);const auto exits=Callers(f);
    const std::array entries{CompletionEntry{f.Edge(0,1),
      {PendingCompletionKind::kReturn,CompletionEntryPayload{7},CompletionCaller{}},source}};
    const auto r=Run(f,entries,exits);Check(r.leaving.size()==2);
    for(const auto& out:r.leaving) {
      if(out.pending.kind==PendingCompletionKind::kReturn) Check(out.pending.payload==CompletionPayload{CompletionEntryPayload{7}});
      else Check(out.pending.payload==CompletionPayload{CompletionExceptionValue{f.Node(call,ExceptionPhase::kBefore)}});
    }
    auto missing=exits;missing.pop_back();
    Check(std::get<CompletionRelationFailure>(AnalyzeCompletionRelation(
        f.body,kProfile,kFrame,f.flow,f.ssa,entries,missing)).kind==CompletionRelationError::kExit);
  };
  check(renamed,5);
  // The same data flow through a register copy and an explicit block split.
  auto split=test::Body({0xd5,0x60,0,0x61,3,0x45,6,3,0x4d,2,0x29,0,0x60,6,0x64,
                        0x61,4,0x60,4,0xfe,0});
  abc::TryRegion region;region.offset={10};region.length=2;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={15};handler.length=6;
  split.tries={region};split.handlers={handler};check(Fixture(std::move(split)),10);
}
void Cycles() {
  Fixture closed(test::Body({0xd5,0x4d,0}));
  const std::array entries{CompletionEntry{closed.Edge(0,1),{PendingCompletionKind::kNormal,{},CompletionCaller{}}}};
  const auto cycle=Run(closed,entries,{});
  Check(cycle.states==2 && cycle.leaving.empty());  // NOT a termination certificate.
  Fixture swap(test::Body({0xd5,0x45,3,0,0x45,4,1,0x60,2,0x4f,13,
                          0x45,5,3,0x45,3,4,0x45,4,5,0x4d,0xf3,0x60,3,0x64}));
  const auto a=swap.Read(1),b=swap.Read(4);
  const std::array contexts{CompletionEntry{swap.Edge(0,1),{PendingCompletionKind::kReturn,a,CompletionCaller{}}}};
  const auto exits=Callers(swap);const auto relation=Run(swap,contexts,exits);
  Check(relation.leaving.size()==2);
  Check(std::any_of(relation.leaving.begin(),relation.leaving.end(),[&](const auto& e){return e.pending.payload==CompletionPayload{a};}));
  Check(std::any_of(relation.leaving.begin(),relation.leaving.end(),[&](const auto& e){return e.pending.payload==CompletionPayload{b};}));
  const auto exhausted=AnalyzeCompletionRelation(swap.body,kProfile,kFrame,swap.flow,swap.ssa,contexts,exits,{{},1});
  Check(std::get<CompletionRelationFailure>(exhausted).kind==CompletionRelationError::kResourceLimit);
}
void Scale() {
  for(const unsigned n:{200u,201u,400u,800u}) {
    std::vector<std::uint8_t> bytes(n,0xd5);bytes.push_back(0x65);Fixture f(test::Body(bytes));
    std::vector<CompletionEntry> entries;
    std::vector<CompletionExit> cuts;
    for(unsigned i=0;i<n;++i) {
      entries.push_back({f.Edge(i,i+1),{PendingCompletionKind::kNormal,{},CompletionCaller{}}});
      cuts.push_back(CompletionEdge{f.Node(i+1,ExceptionPhase::kBefore),0});
    }
    const auto r=Run(f,entries,cuts);Check(r.leaving.size()==n);
    // Producer and independent verifier have separate exact work grants.
    std::size_t low=0,high=r.peak_bytes;
    const auto analyze=[&](CompletionRelationBudget b){return AnalyzeCompletionRelation(f.body,kProfile,kFrame,f.flow,f.ssa,entries,cuts,b);};
    Check(std::holds_alternative<CompletionRelation>(analyze({{high}})));
    while(low<high) {
      const auto mid=low+(high-low)/2;
      if(std::holds_alternative<CompletionRelation>(analyze({{mid}}))) high=mid;else low=mid+1;
    }
    Check(std::holds_alternative<CompletionRelation>(analyze({{low,r.work}})));
    Check(std::get<CompletionRelationFailure>(analyze({{low-1}})).kind==CompletionRelationError::kResourceLimit);
    Check(std::get<CompletionRelationFailure>(analyze({{low,r.work-1}})).kind==CompletionRelationError::kWorkLimit);
    Check(std::get<CompletionRelationFailure>(analyze({{},n-1})).kind==CompletionRelationError::kResourceLimit);
    const auto verified=std::get<CompletionRelationCost>(VerifyCompletionRelation(
        f.body,kProfile,kFrame,f.flow,f.ssa,entries,cuts,r));
    const auto verify=[&](CompletionRelationBudget b){return VerifyCompletionRelation(
        f.body,kProfile,kFrame,f.flow,f.ssa,entries,cuts,r,b);};
    Check(std::holds_alternative<CompletionRelationCost>(verify({{verified.peak_bytes,verified.work}})));
    Check(std::get<CompletionRelationFailure>(verify({{verified.peak_bytes-1}})).kind==CompletionRelationError::kResourceLimit);
    Check(std::get<CompletionRelationFailure>(verify({{verified.peak_bytes,verified.work-1}})).kind==CompletionRelationError::kWorkLimit);
    const auto callers=Callers(f);
    // Dense contexts exceed linear sparse-state admission: no partial result.
    Check(std::get<CompletionRelationFailure>(AnalyzeCompletionRelation(
        f.body,kProfile,kFrame,f.flow,f.ssa,entries,callers)).kind==CompletionRelationError::kResourceLimit);
    const std::span one(entries.data(),1);
    Check(Run(f,one,callers).leaving.size()==1);
    std::vector<std::uint8_t> copies{0xd5};
    for(unsigned i=0;i<n;++i) copies.insert(copies.end(),{0x45,3,0,0xd5});
    copies.insert(copies.end(),{0x60,3,0x64});Fixture d(test::Body(copies));
    const auto value=d.Read(1);
    const std::array deep_entries{CompletionEntry{d.Edge(0,1),{PendingCompletionKind::kReturn,value,CompletionCaller{}}}};
    const auto deep_cuts=Callers(d);const auto deep=Run(d,deep_entries,deep_cuts);
    Check(deep.leaving.size()==1 && deep.leaving[0].pending.payload==CompletionPayload{value});
    const auto deep_query=[&](CompletionRelationBudget b){return AnalyzeCompletionRelation(
        d.body,kProfile,kFrame,d.flow,d.ssa,deep_entries,deep_cuts,b);};
    std::size_t dl=0,dh=deep.peak_bytes;
    while(dl<dh) {
      const auto mid=dl+(dh-dl)/2;
      if(std::holds_alternative<CompletionRelation>(deep_query({{mid}}))) dh=mid;else dl=mid+1;
    }
    Check(std::holds_alternative<CompletionRelation>(deep_query({{dl,deep.work}})));
    Check(std::get<CompletionRelationFailure>(deep_query({{dl-1}})).kind==CompletionRelationError::kResourceLimit);
    std::cout<<"n="<<n<<" width-states="<<r.states<<" width-exact="<<low<<" width-work="<<r.work
             <<" depth-states="<<deep.states<<" depth-exact="<<dl<<" depth-work="<<deep.work<<'\n';
  }
}
}
int main(){Correlation();ClosePriority();Nested();Targets();Variants();Cycles();Scale();}

