#include "../../../src/source/close_completion.h"
#include "abcd/analysis/close_outcome_relation.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
ExceptionNodeId Node(const ExceptionFlow& flow, unsigned pc, ExceptionPhase phase) {
  for(std::uint32_t n=0;n<flow.nodes.size();++n)
    if(flow.nodes[n].phase==phase && flow.instructions[flow.nodes[n].instruction.value].pc.value==pc) return {n};
  Check(false);return {};
}
enum class Mutation { kNone,kRegisters,kSplitDone,kNonNullish,kNull,kAliasSentinel,kNotEqual,kStrictEqual,kStrictNotEqual };
void Method(const IteratorKeyInput& file,const abc::MethodInfo& method,unsigned padding=0,Mutation mutation=Mutation::kNone) {
  auto body=std::get<abc::MethodBody>(abc::DecodeMethodBody(file.file,file.header.profile,*method.code));
  if(mutation==Mutation::kSplitDone) {
    const auto end=body.instructions.back().offset.value+body.instructions.back().decoded.size;
    const auto at=std::find_if(body.instructions.begin(),body.instructions.end(),[](const auto& i){return i.offset.value==56;});
    const auto target=*at->branch_target;
    auto jump=test::Body({0x98,0,0,0,0}).instructions.front();jump.offset={end};
    jump.branch_target=target;
    jump.decoded.branch_delta=std::int64_t(jump.branch_target->value)-end;
    jump.decoded.operands[0].raw_bits=static_cast<std::uint32_t>(*jump.decoded.branch_delta);
    at->branch_target=abc::CodeOffset{end};at->decoded.branch_delta=std::int64_t(end)-at->offset.value;
    at->decoded.operands[0].raw_bits=static_cast<std::uint32_t>(*at->decoded.branch_delta);
    body.instructions.push_back(jump);
  }
  if(mutation!=Mutation::kNone)for(auto& i:body.instructions) {
    if(i.offset.value==113 && (mutation==Mutation::kNonNullish || mutation==Mutation::kNull))
      i.decoded=test::Body({static_cast<std::uint8_t>(mutation==Mutation::kNull?0x01:0x02)}).instructions.front().decoded;
    if(i.offset.value==130 && mutation==Mutation::kAliasSentinel)i.decoded.operands[0].raw_bits=10;
    if(i.offset.value==114) {
      if(mutation==Mutation::kNotEqual)i.decoded.opcode=abc::Opcode::kNOTEQ_IMM8_V8;
      if(mutation==Mutation::kStrictEqual)i.decoded.opcode=abc::Opcode::kSTRICTEQ_IMM8_V8;
      if(mutation==Mutation::kStrictNotEqual)i.decoded.opcode=abc::Opcode::kSTRICTNOTEQ_IMM8_V8;
    }
    if(mutation==Mutation::kRegisters)for(auto& operand:i.decoded.operands)
      if(operand.kind==abc::OperandKind::kRegister) {
        if(operand.raw_bits==7)operand.raw_bits=15;else if(operand.raw_bits==15)operand.raw_bits=7;
      }
  }

  if(padding) {
    // Grow the actually traversed close path, not merely an unreachable or
    // pre-entry prefix. Test-only PCs identify the existing fixture boundary.
    const auto at=method.offset.value==423?97u:89u;
    const auto shift=[&](std::uint32_t pc){return pc>=at?pc+padding:pc;};
    for(auto& i:body.instructions) {
      i.offset.value=shift(i.offset.value);
      if(i.branch_target){i.branch_target->value=shift(i.branch_target->value);i.decoded.branch_delta=std::int64_t(i.branch_target->value)-i.offset.value;}
    }
    for(auto& t:body.tries){const auto end=shift(t.offset.value+t.length);t.offset.value=shift(t.offset.value);t.length=end-t.offset.value;}
    for(auto& h:body.handlers){const auto end=shift(h.offset.value+h.length);h.offset.value=shift(h.offset.value);h.length=end-h.offset.value;}
    const auto nop=test::Body({0xd5}).instructions.front();
    const auto pos=std::lower_bound(body.instructions.begin(),body.instructions.end(),at,[](const auto& i,unsigned pc){return i.offset.value<pc;});
    const auto first=pos-body.instructions.begin();body.instructions.insert(pos,padding,nop);
    for(unsigned i=0;i<padding;++i)body.instructions[first+i].offset={at+i};
  }
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers,method.code->arguments,{}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,file.header.profile,frame.registers,{}, {},&frame));
  IdfQuota work{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,work));
  std::vector<EntryValueFact> entries;
  for(std::uint32_t v=0;v<ssa.Values().size();++v)
    if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=method.code->registers)
        entries.push_back({{v},ValueKinds::Language()});
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,work,{},
      PendingExceptionValues::kLanguage,{}, {},CallResultValues::kLanguage,FrameReceiver::kInitialized));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values,
      kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};
  const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto keys=std::get<IteratorKeys>(ResolveIteratorKeys(file,method.offset,graph));
  const auto scan=std::get<IteratorProtocolScan>(ScanIteratorProtocols(graph,input,regions,keys));
  const auto derived=std::get<CloseEntries>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan));
  for(const auto& c:derived.closes) {
    const auto read=graph.phases[graph.ops[c.read.value].block.value].input;
    std::optional<ExceptionNodeId> call;
    for(std::uint32_t o=0;o<graph.ops.size();++o) {
      const auto& v=graph.ops[o];if(!std::holds_alternative<ValueInvoke>(v.operation))continue;
      // Test oracle locates the actual fixture call independently by its PC.
      const auto n=graph.phases[v.block.value].input;
      const auto pc=flow.instructions[flow.nodes[n.value].instruction.value].pc.value;
      const auto rpc=flow.instructions[flow.nodes[read.value].instruction.value].pc.value;
      if(pc==rpc+16)call=n;
    }
    Check(call.has_value());
    std::vector<CompletionEntry> incoming;for(const auto& e:c.entries)incoming.push_back(e.entry);
    auto analyzed=AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits);
    Check(std::holds_alternative<CloseOutcomeRelation>(analyzed));
    const auto& relation=std::get<CloseOutcomeRelation>(analyzed);
    if(incoming.front().pending.kind!=PendingCompletionKind::kThrow)Check(!relation.comparisons.empty());
    const auto verified=VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,relation);
    Check(std::holds_alternative<CompletionRelationCost>(verified));
    std::array<bool,6> kinds{};
    std::vector<CompletionLeaving> projection;
    bool sentinel=false;
    for(const auto& x:relation.leaving) {
      kinds[static_cast<unsigned>(x.outcome.kind)]=true;
      sentinel|=x.outcome.gap==CloseOutcomeGap::kSentinelPath;
      projection.push_back({x.entry,x.exit,x.pending});
      if(x.outcome.kind==CloseOutcomeKind::kGetterThrow || x.outcome.kind==CloseOutcomeKind::kCallThrow ||
          x.outcome.kind==CloseOutcomeKind::kPrimitiveResult) {
        Check(x.outcome.event && x.outcome.exception);
        Check(*x.outcome.exception==CompletionPayload{CompletionExceptionValue{*x.outcome.event}});
      }
      if(incoming[x.entry].pending.kind!=PendingCompletionKind::kThrow &&
          (x.outcome.kind==CloseOutcomeKind::kGetterThrow || x.outcome.kind==CloseOutcomeKind::kCallThrow) &&
          (x.pending.kind!=PendingCompletionKind::kThrow || x.pending.payload!=*x.outcome.exception))
        Check(x.outcome.gap==CloseOutcomeGap::kSentinelPath);
      if(x.outcome.gap==CloseOutcomeGap::kNone &&
          (x.outcome.kind==CloseOutcomeKind::kSuccessObject || x.outcome.kind==CloseOutcomeKind::kMissingOrNullish))
        Check(x.pending==incoming[x.entry].pending);
      if(!padding && mutation==Mutation::kNone)
      std::cout<<"m="<<method.offset.value<<" read="<<flow.instructions[flow.nodes[read.value].instruction.value].pc.value
        <<" entry="<<x.entry<<" kind="<<unsigned(x.pending.kind)<<" outcome="<<unsigned(x.outcome.kind)
        <<" event="<<(x.outcome.event?flow.instructions[flow.nodes[x.outcome.event->value].instruction.value].pc.value:UINT32_MAX)
        <<" gap="<<unsigned(x.outcome.gap)<<'\n';
    }
    std::sort(projection.begin(),projection.end());projection.erase(std::unique(projection.begin(),projection.end()),projection.end());
    Check(std::includes(c.relation.leaving.begin(),c.relation.leaving.end(),projection.begin(),projection.end()));
    const auto read_pc=flow.instructions[flow.nodes[read.value].instruction.value].pc.value;
    const bool ordinary=mutation==Mutation::kNone || mutation==Mutation::kRegisters || mutation==Mutation::kSplitDone || mutation==Mutation::kNull;
    if(method.offset.value==423 && !padding && read_pc==105) {
      const auto fact_at=[&](unsigned pc){return std::find_if(relation.comparisons.begin(),relation.comparisons.end(),[&](const auto& f){
        return flow.instructions[flow.nodes[f.node.value].instruction.value].pc.value==pc;});};
      const auto nullish=fact_at(114);
      Check((nullish!=relation.comparisons.end())==(mutation!=Mutation::kNonNullish));
      if(nullish!=relation.comparisons.end())Check(nullish->kind==ComparisonProofKind::kNullishOperand);
      if(mutation==Mutation::kAliasSentinel)Check(fact_at(139)==relation.comparisons.end());
      else {
        const auto hole=fact_at(139);Check(hole!=relation.comparisons.end());
        Check(hole->left==ComparisonDomain::kHole && hole->right==ComparisonDomain::kHole && hole->equal==true);
      }
      const auto bad_pc=mutation==Mutation::kNonNullish?114u:139u;
      Check(std::any_of(relation.leaving.begin(),relation.leaving.end(),[&](const auto& x){
        return x.outcome.gap==CloseOutcomeGap::kOtherException && x.outcome.event &&
          flow.instructions[flow.nodes[x.outcome.event->value].instruction.value].pc.value==bad_pc;}));
    }
    if(ordinary && incoming.front().pending.kind!=PendingCompletionKind::kThrow) {
      for(unsigned k=1;k<kinds.size();++k)Check(kinds[k]);
      Check(sentinel);
      for(const auto& x:relation.leaving)if(x.outcome.gap==CloseOutcomeGap::kSentinelPath) {
        Check(x.outcome.uncertain_at.has_value());
        Check(flow.instructions[flow.nodes[x.outcome.uncertain_at->value].instruction.value].pc.value==read_pc+37);
      }
      const auto uncertain=std::find_if(relation.leaving.begin(),relation.leaving.end(),[](const auto& x){return x.outcome.gap==CloseOutcomeGap::kSentinelPath;});
      auto cleared=relation;cleared.leaving[uncertain-relation.leaving.begin()].outcome.gap=CloseOutcomeGap::kNone;
      Check(std::get<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,cleared)).kind==CompletionRelationError::kCorrespondence);
      // A caught close error must keep the throwing site's identity, not the
      // entry's Break/Return payload. The conservative Hole branch is labelled.
      for(const auto kind:{CloseOutcomeKind::kGetterThrow,CloseOutcomeKind::kCallThrow,CloseOutcomeKind::kPrimitiveResult}) {
        const auto good=std::find_if(relation.leaving.begin(),relation.leaving.end(),[&](const auto& x){
          return x.outcome.kind==kind && x.pending.kind==PendingCompletionKind::kThrow &&
              x.outcome.exception && x.pending.payload==*x.outcome.exception;});
        Check(good!=relation.leaving.end());
        auto replaced=relation;
        replaced.leaving[good-relation.leaving.begin()].pending.payload=incoming[good->entry].pending.payload;
        Check(std::get<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,replaced)).kind==CompletionRelationError::kCorrespondence);
      }
      const auto primitive=std::find_if(relation.leaving.begin(),relation.leaving.end(),[](const auto& x){return x.outcome.kind==CloseOutcomeKind::kPrimitiveResult;});
      auto success=relation;success.leaving[primitive-relation.leaving.begin()].outcome.kind=CloseOutcomeKind::kSuccessObject;
      Check(std::get<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,success)).kind==CompletionRelationError::kCorrespondence);
    }
    auto bad=relation;Check(!bad.leaving.empty());
    if(!bad.comparisons.empty()) {
      bad.comparisons.front().left=bad.comparisons.front().left==ComparisonDomain::kUnknown?ComparisonDomain::kHole:ComparisonDomain::kUnknown;
      Check(std::holds_alternative<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,bad)));
    }
    bad=relation;
    bad.leaving[0].outcome.kind=bad.leaving[0].outcome.kind==CloseOutcomeKind::kSuccessObject?
        CloseOutcomeKind::kPrimitiveResult:CloseOutcomeKind::kSuccessObject;
    Check(std::holds_alternative<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,bad)));
    bad=relation;bad.leaving[0].outcome.exception=incoming[bad.leaving[0].entry].pending.payload;
    Check(std::holds_alternative<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,bad)));
    bad=relation;bad.leaving[0].pending.payload=CompletionEntryPayload{99};
    Check(std::holds_alternative<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,bad)));

    // Exact producer admission and verifier work boundaries on each query.
    std::size_t low=0,high=relation.peak_bytes;
    while(low<high) {
      const auto mid=low+(high-low)/2;
      if(std::holds_alternative<CloseOutcomeRelation>(AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,{{mid}})))high=mid;
      else low=mid+1;
    }
    Check(std::holds_alternative<CloseOutcomeRelation>(AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,{{low}})));
    Check(low && std::get<CompletionRelationFailure>(AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,{{low-1}})).kind==CompletionRelationError::kResourceLimit);
    Check(std::holds_alternative<CloseOutcomeRelation>(AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,{{64*1024*1024,relation.work}})));
    Check(std::get<CompletionRelationFailure>(AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,{{64*1024*1024,relation.work-1}})).kind==CompletionRelationError::kWorkLimit);
    const auto cost=std::get<CompletionRelationCost>(verified);
    Check(std::holds_alternative<CompletionRelationCost>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,relation,{{64*1024*1024,cost.work}})));
    Check(std::get<CompletionRelationFailure>(VerifyCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,relation,{{64*1024*1024,cost.work-1}})).kind==CompletionRelationError::kWorkLimit);
    Check(std::get<CompletionRelationFailure>(AnalyzeCloseOutcomeRelation(body,file.header.profile,frame.registers,flow,ssa,{read,*call},incoming,c.exits,{{64*1024*1024},0})).kind==CompletionRelationError::kResourceLimit);
    std::cout<<"m="<<method.offset.value<<" padding="<<padding<<" read="<<flow.instructions[flow.nodes[read.value].instruction.value].pc.value
      <<" states="<<relation.states<<" bytes="<<low<<" work="<<relation.work<<'\n';
  }

}
void PriorityTable() {
  for(const auto kind:{PendingCompletionKind::kBreak,PendingCompletionKind::kContinue,PendingCompletionKind::kReturn}) {
    PendingCompletion entry{kind,{},ExceptionNodeId{9}};
    if(kind==PendingCompletionKind::kReturn){entry.payload=SsaValueId{3};entry.target=CompletionCaller{};}
    for(const auto outcome:{CloseOutcomeKind::kSuccessObject,CloseOutcomeKind::kMissingOrNullish,
        CloseOutcomeKind::kGetterThrow,CloseOutcomeKind::kCallThrow,CloseOutcomeKind::kPrimitiveResult}) {
      CloseOutcomeLeaving x{0,CompletionCallerExit{{12}},entry,{outcome,ExceptionNodeId{5},{}}};
      if(outcome==CloseOutcomeKind::kGetterThrow || outcome==CloseOutcomeKind::kCallThrow || outcome==CloseOutcomeKind::kPrimitiveResult) {
        x.outcome.exception=CompletionExceptionValue{{5}};
        x.pending={PendingCompletionKind::kThrow,*x.outcome.exception,CompletionCaller{}};
      }
      Check(!ClosePriorityMismatch(entry,x));
      auto bad=x;
      if(x.outcome.exception) {
        bad.pending.payload=entry.payload;
        Check(ClosePriorityMismatch(entry,bad)==(outcome==CloseOutcomeKind::kPrimitiveResult?
            ClosePriorityReason::kExpectedTypeError:ClosePriorityReason::kExpectedCloseError));
        bad=x;bad.outcome.exception=entry.payload;
        Check(ClosePriorityMismatch(entry,bad).has_value());
        bad=x;bad.outcome.kind=CloseOutcomeKind::kSuccessObject;
        Check(ClosePriorityMismatch(entry,bad)==ClosePriorityReason::kExpectedOriginal);
      } else {
        bad.pending={PendingCompletionKind::kThrow,CompletionExceptionValue{{5}},CompletionCaller{}};
        Check(ClosePriorityMismatch(entry,bad)==ClosePriorityReason::kExpectedOriginal);
      }
      bad=x;bad.outcome.gap=CloseOutcomeGap::kSentinelPath;
      Check(ClosePriorityMismatch(entry,bad)==ClosePriorityReason::kSentinelPath);
    }
  }
  const PendingCompletion thrown{PendingCompletionKind::kThrow,SsaValueId{7},CompletionCaller{}};
  CloseOutcomeLeaving x{0,CompletionCallerExit{{12}},thrown,{CloseOutcomeKind::kGetterThrow,ExceptionNodeId{5},CompletionExceptionValue{{5}}}};
  Check(!ClosePriorityMismatch(thrown,x));x.pending.payload=*x.outcome.exception;
  Check(ClosePriorityMismatch(thrown,x)==ClosePriorityReason::kExpectedOriginal);
}

}
int main(int argc,char** argv) {
  Check(argc==2);PriorityTable();
  const auto file=std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash=std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  unsigned tested=0;
  for(const auto& method:index.methods) if(method.offset.value==423 || method.offset.value==451) {
    ++tested;
    Method({*file,header,index},method);
    Method({*file,header,index},method,0,Mutation::kRegisters);
    Method({*file,header,index},method,0,Mutation::kSplitDone);
    for(unsigned n:{200,201,400,800})Method({*file,header,index},method,n);
    if(method.offset.value==423)for(const auto m:{Mutation::kNonNullish,Mutation::kNull,Mutation::kAliasSentinel,
        Mutation::kNotEqual,Mutation::kStrictEqual,Mutation::kStrictNotEqual})Method({*file,header,index},method,0,m);

  }
  Check(tested==2);
}
