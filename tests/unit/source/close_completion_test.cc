#include "../../../src/source/close_completion.h"
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
enum class Mutation { kNone,kInitiallyClosed,kReset,kReceiver,kInvertedDone,kRegisters,kSplitDone,kCycleExit };
void Method(const IteratorKeyInput& file,const abc::MethodInfo& method,unsigned padding=0,Mutation mutation=Mutation::kNone,SourceContract contract=kSourceContract) {
  auto body=std::get<abc::MethodBody>(abc::DecodeMethodBody(file.file,file.header.profile,*method.code));
  if(mutation==Mutation::kSplitDone || mutation==Mutation::kCycleExit) {
    const auto end=body.instructions.back().offset.value+body.instructions.back().decoded.size;
    const auto at=std::find_if(body.instructions.begin(),body.instructions.end(),[](const auto& i){return i.offset.value==56;});
    const auto target=*at->branch_target;
    auto jump=test::Body({0x98,0,0,0,0}).instructions.front();jump.offset={end};
    jump.branch_target=mutation==Mutation::kCycleExit?abc::CodeOffset{end}:target;
    jump.decoded.branch_delta=std::int64_t(jump.branch_target->value)-end;
    jump.decoded.operands[0].raw_bits=static_cast<std::uint32_t>(*jump.decoded.branch_delta);
    at->branch_target=abc::CodeOffset{end};at->decoded.branch_delta=std::int64_t(end)-at->offset.value;
    at->decoded.operands[0].raw_bits=static_cast<std::uint32_t>(*at->decoded.branch_delta);
    body.instructions.push_back(jump);
  }
  if(mutation==Mutation::kReset) {
    const unsigned at=method.offset.value==423?103:94;
    auto inserted=test::Body({0x03,0x61,0x07}).instructions;
    const auto shift=[&](std::uint32_t pc){return pc>=at?pc+3:pc;};
    for(auto& i:body.instructions) {
      i.offset.value=shift(i.offset.value);
      if(i.branch_target){i.branch_target->value=shift(i.branch_target->value);i.decoded.branch_delta=std::int64_t(i.branch_target->value)-i.offset.value;}
    }
    for(auto& t:body.tries){const auto end=shift(t.offset.value+t.length);t.offset.value=shift(t.offset.value);t.length=end-t.offset.value;}
    for(auto& h:body.handlers){const auto end=shift(h.offset.value+h.length);h.offset.value=shift(h.offset.value);h.length=end-h.offset.value;}
    for(auto& i:inserted)i.offset.value+=at;
    const auto pos=std::lower_bound(body.instructions.begin(),body.instructions.end(),at,[](const auto& i,unsigned pc){return i.offset.value<pc;});
    body.instructions.insert(pos,inserted.begin(),inserted.end());
  }
  if(mutation!=Mutation::kNone)for(auto& i:body.instructions) {
    if(mutation==Mutation::kRegisters)for(auto& operand:i.decoded.operands)
      if(operand.kind==abc::OperandKind::kRegister) {
        if(operand.raw_bits==7)operand.raw_bits=15;else if(operand.raw_bits==15)operand.raw_bits=7;
      }
    if(mutation==Mutation::kInitiallyClosed && i.offset.value==34)i.decoded=test::Body({0x02}).instructions[0].decoded;
    if(mutation==Mutation::kReceiver && i.offset.value==(method.offset.value==423?121:112))i.decoded.operands[1].raw_bits=10;
    if(mutation==Mutation::kInvertedDone && i.offset.value==56) {
      i.decoded.opcode=i.decoded.opcode==abc::Opcode::kJNEZ_IMM16?abc::Opcode::kJEQZ_IMM16:abc::Opcode::kJEQZ_IMM8;
    }
  }
  if(padding) {
    const auto nop=test::Body({0xd5}).instructions.front();
    for(auto& instruction:body.instructions) {
      instruction.offset.value+=padding;
      if(instruction.branch_target) instruction.branch_target->value+=padding;
    }
    for(auto& region:body.tries) region.offset.value+=padding;
    for(auto& handler:body.handlers) handler.offset.value+=padding;
    body.instructions.insert(body.instructions.begin(),padding,nop);
    for(unsigned i=0;i<padding;++i) body.instructions[i].offset={i};
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
  const auto analyze=[&](SemanticBudget budget=SemanticBudget{}) {
    return AnalyzeCloseCompletion(body,file.header.profile,graph,input,regions,keys,scan,budget,contract);
  };
  const auto verify=[&](const CloseCompletionSummary& summary,SemanticBudget budget=SemanticBudget{}) {
    return VerifyCloseCompletion(body,file.header.profile,graph,input,regions,keys,scan,summary,budget,contract);
  };
  auto result=analyze();
  if(mutation!=Mutation::kNone && mutation!=Mutation::kRegisters && mutation!=Mutation::kSplitDone) {
    Check(std::holds_alternative<CloseCompletionFailure>(result));return;
  }
  if(const auto* f=std::get_if<CloseCompletionFailure>(&result)) {
    std::cerr<<"failure="<<unsigned(f->kind)<<" method="<<method.offset.value<<'\n';Check(false);
  }
  const auto& summary=std::get<CloseCompletionSummary>(result);
  if(ExternalExceptionNoHole(contract)==ExternalExceptionPolicy::kNoHoleV1)for(const auto& r:summary.regions) {
    Check(r.outcomes && !r.outcomes->events.empty());
    for(const auto& m:r.members)Check(m.role!=CloseMemberRole::kUnproved);
  }
  Check(summary.regions.size()==(method.offset.value==423?3:1));
  const auto verified=verify(summary);
  if(const auto* f=std::get_if<CloseCompletionFailure>(&verified)) {
    std::cerr<<"verify failure="<<unsigned(f->kind)<<" method="<<method.offset.value<<'\n';Check(false);
  }
  for(const auto& e:summary.entries) {
    Check(e.obligations[static_cast<unsigned>(CloseObligation::kGuard)].state==CloseProofState::kVerified);
    Check(e.obligations[static_cast<unsigned>(CloseObligation::kReference)].state==CloseProofState::kVerified);
    const auto priority=e.obligations[static_cast<unsigned>(CloseObligation::kPriority)].state;
    if(e.pending.kind==PendingCompletionKind::kThrow)Check(priority==CloseProofState::kVerified);
    else if(ExternalExceptionNoHole(contract)==ExternalExceptionPolicy::kNoHoleV1) {
      if(priority!=CloseProofState::kVerified) {
        const auto& f=*e.obligations[static_cast<unsigned>(CloseObligation::kPriority)].missing;
        std::cerr<<"V4 missing pc="<<(f.node?flow.instructions[flow.nodes[f.node->value].instruction.value].pc.value:UINT32_MAX)<<'\n';
      }
      Check(priority==CloseProofState::kVerified);
    } else {
      Check(priority==CloseProofState::kMissing); // Conservative paths are NOT a pass.
      const auto& missing=e.obligations[static_cast<unsigned>(CloseObligation::kPriority)].missing;
      Check(missing && missing->priority && missing->node && missing->outcome_gap);
      if(!padding && mutation==Mutation::kNone)std::cout<<"priority-gap="<<unsigned(*missing->priority)
        <<" outcome-gap="<<unsigned(*missing->outcome_gap)<<" pc="
        <<flow.instructions[flow.nodes[missing->node->value].instruction.value].pc.value<<'\n';
    }
    Check(e.obligations[static_cast<unsigned>(CloseObligation::kOwnership)].state==CloseProofState::kMissing);
    if(!padding && mutation==Mutation::kNone)std::cout<<"entry-pc="<<flow.instructions[flow.nodes[e.source.source.value].instruction.value].pc.value
      <<" contract="<<SourceContractName(contract)<<" kind="<<unsigned(e.pending.kind)<<" priority="<<unsigned(priority)<<" ownership=missing\n";
  }
  const auto reject=[&](const CloseCompletionSummary& bad) {
    Check(std::holds_alternative<CloseCompletionFailure>(verify(bad)));
  };
  if(ExternalExceptionNoHole(contract)==ExternalExceptionPolicy::kNoHoleV1) {
    Check(std::holds_alternative<CloseCompletionFailure>(VerifyCloseCompletion(body,file.header.profile,graph,input,regions,keys,scan,summary,{},SourceContract::kLanguageSourceV3)));
    auto forged=summary;
    Check(!forged.regions[0].outcomes->catch_payloads.empty());
    auto& fact=forged.regions[0].outcomes->catch_payloads.front();
    fact.domain=fact.domain==CatchPayloadDomain::kNonHole?CatchPayloadDomain::kHolePossible:CatchPayloadDomain::kNonHole;
    reject(forged);
  }
  auto bad=summary;bad.method.value+=1;reject(bad);
  bad=summary;bad.entries[0].pending.payload=CompletionEntryPayload{99};reject(bad);
  bad=summary;bad.entries[0].pending.target=ExceptionNodeId{0};reject(bad);
  bad=summary;bad.entries.erase(bad.entries.begin());reject(bad);
  bad=summary;bad.entries[0].obligations[static_cast<unsigned>(CloseObligation::kOwnership)]={CloseProofState::kVerified,{}};reject(bad);
  bad=summary;bad.regions[0].members.pop_back();reject(bad);
  bad=summary;bad.regions[0].edges.pop_back();reject(bad);
  bad=summary;bad.regions[0].call=OpId{0};reject(bad);
  Check(summary.regions[0].outcomes && !summary.regions[0].outcomes->leaving.empty());
  bad=summary;bad.regions[0].outcomes->leaving[0].outcome.kind=CloseOutcomeKind::kSuccessObject;reject(bad);
  Check(!summary.regions[0].outcomes->comparisons.empty());
  bad=summary;bad.regions[0].outcomes->comparisons.clear();reject(bad);
  bad=summary;bad.regions[0].outcomes->events.clear();reject(bad);
  bad=summary;bad.regions[0].outcomes->events.front().proved=!bad.regions[0].outcomes->events.front().proved;reject(bad);
  bad=summary;bad.regions[0].outcomes->events.front().node=ExceptionNodeId{0};reject(bad);
  bad=summary;bad.regions[0].members.push_back(bad.regions[0].members.front());reject(bad);
  bad=summary;bad.regions[0].edges[0].successor=2;reject(bad);
  bad=summary;bad.entries[0].obligations[static_cast<unsigned>(CloseObligation::kPriority)]={CloseProofState::kVerified,{}};
  if(summary.entries[0].pending.kind!=PendingCompletionKind::kThrow && ExternalExceptionNoHole(contract)==ExternalExceptionPolicy::kPinnedRuntime)reject(bad);
  bad=summary;bad.entries[0].obligations[static_cast<unsigned>(CloseObligation::kGuard)]={CloseProofState::kMissing,{}};reject(bad);
  Check(std::get<CloseCompletionFailure>(analyze({0})).kind==CloseCompletionError::kResourceLimit);
  Check(std::get<CloseCompletionFailure>(analyze({64*1024*1024,0})).kind==CloseCompletionError::kWorkLimit);
  if(padding || mutation==Mutation::kNone) {
    const auto used=std::get<std::uint64_t>(verified);
    Check(std::holds_alternative<std::uint64_t>(verify(summary,{64*1024*1024,used})));
    Check(std::get<CloseCompletionFailure>(verify(summary,{64*1024*1024,used-1})).kind==CloseCompletionError::kWorkLimit);
    std::size_t low=0,high=summary.peak_bytes;
    while(low<high) {
      const auto mid=low+(high-low)/2;
      if(std::holds_alternative<CloseCompletionSummary>(analyze({mid})))high=mid;
      else low=mid+1;
    }
    Check(std::holds_alternative<CloseCompletionSummary>(analyze({low})));
    Check(low && std::get<CloseCompletionFailure>(analyze({low-1})).kind==CloseCompletionError::kResourceLimit);
    Check(std::holds_alternative<CloseCompletionSummary>(analyze({64*1024*1024,summary.work})));
    Check(std::get<CloseCompletionFailure>(analyze({64*1024*1024,summary.work-1})).kind==CloseCompletionError::kWorkLimit);
    std::cout<<"method="<<method.offset.value<<" padding="<<padding<<" bytes="<<low<<" work="<<summary.work<<'\n';
  }

}
}
int main(int argc,char** argv) {
  Check(argc==2);
  const auto file=std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash=std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  unsigned tested=0;
  for(const auto& method:index.methods) if(method.offset.value==423 || method.offset.value==451) {
    ++tested;
    Method({*file,header,index},method);
    if(method.offset.value==423)for(auto contract:{SourceContract::kLanguageSourceV1,SourceContract::kLanguageSourceV2,SourceContract::kLanguageSourceV3,SourceContract::kLanguageSourceV4})
      Method({*file,header,index},method,0,Mutation::kNone,contract);
    Method({*file,header,index},method,0,Mutation::kRegisters);
    Method({*file,header,index},method,0,Mutation::kSplitDone);
    for(auto mutation:{Mutation::kInitiallyClosed,Mutation::kReset,Mutation::kReceiver,Mutation::kInvertedDone,Mutation::kCycleExit})Method({*file,header,index},method,0,mutation);
    if(method.offset.value==451) for(unsigned n:{200,201,400,800}) Method({*file,header,index},method,n);
  }
  Check(tested==2);
}
