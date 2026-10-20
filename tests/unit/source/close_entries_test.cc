#include "../../../src/source/close_entries.h"
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
void Method(const IteratorKeyInput& file,const abc::MethodInfo& method,unsigned padding=0,Mutation mutation=Mutation::kNone,bool nested=false,unsigned register_shift=0) {
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
  if(register_shift)for(auto& instruction:body.instructions)
    for(auto& operand:instruction.decoded.operands)
      if(operand.kind==abc::OperandKind::kRegister)operand.raw_bits+=register_shift;
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
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers+register_shift,method.code->arguments,{}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,file.header.profile,frame.registers,{}, {},&frame));
  IdfQuota work{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,work));
  std::vector<EntryValueFact> entries;
  for(std::uint32_t v=0;v<ssa.Values().size();++v)
    if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=method.code->registers+register_shift)
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
  auto result=DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan);
  if(mutation!=Mutation::kNone && mutation!=Mutation::kRegisters && mutation!=Mutation::kSplitDone) {
    Check(std::holds_alternative<CloseEntryFailure>(result));
    const auto kind=std::get<CloseEntryFailure>(result).kind;
    switch(mutation) {
      case Mutation::kInitiallyClosed:case Mutation::kReset:Check(kind==CloseEntryError::kGuard);break;
      case Mutation::kReceiver:Check(kind==CloseEntryError::kAcquisition);break;
      case Mutation::kInvertedDone:Check(kind==CloseEntryError::kExternalEntry);break;
      case Mutation::kCycleExit:Check(kind==CloseEntryError::kContinuation);break;
      case Mutation::kNone:case Mutation::kRegisters:case Mutation::kSplitDone:Check(false);break;
    }
    return;
  }
  if(const auto* f=std::get_if<CloseEntryFailure>(&result)) {
    std::cerr<<"method="<<method.offset.value<<" error="<<unsigned(f->kind)<<" read="
      <<(f->read?f->read->value:UINT32_MAX)<<" node="<<(f->node?f->node->value:UINT32_MAX)
      <<" pc="<<(f->node?flow.instructions[flow.nodes[f->node->value].instruction.value].pc.value:UINT32_MAX)<<'\n';Check(false);
  }
  const auto& derived=std::get<CloseEntries>(result);
  Check(nested?derived.closes.size()>=4:derived.closes.size()==(method.offset.value==423?3:1));
  Check(std::holds_alternative<std::uint64_t>(VerifyCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,derived)));
  for(const auto& close:derived.closes) {
    Check(close.precision==CloseEntryPrecision::kConservativeSentinelValues);
    if(!padding && mutation==Mutation::kNone) {
      const auto read_pc=flow.instructions[flow.nodes[graph.phases[graph.ops[close.read.value].block.value].input.value].instruction.value].pc.value;
      std::cout<<"  read="<<read_pc<<" entries="<<close.entries.size()<<" may-exits="<<close.relation.leaving.size()<<'\n';
      for(const auto& e:close.entries) {
        const auto source=flow.instructions[flow.nodes[e.entry.edge.source.value].instruction.value].pc.value;
        const auto target=flow.nodes[e.entry.edge.source.value].successors[e.entry.edge.successor].target;
        std::cout<<"    edge="<<source<<"->"<<flow.instructions[flow.nodes[target.value].instruction.value].pc.value
          <<" kind="<<unsigned(e.entry.pending.kind)<<" origin="
          <<flow.instructions[flow.nodes[e.origin.value].instruction.value].pc.value<<'\n';
      }
    }
  }
  const auto find=[&](unsigned pc)->const DerivedClose& {
    const auto id=Node(flow,pc+padding,ExceptionPhase::kBefore);
    const auto it=std::find_if(derived.closes.begin(),derived.closes.end(),[&](const auto& c){
      return graph.phases[graph.ops[c.read.value].block.value].input==id;});
    Check(it!=derived.closes.end());return *it;
  };
  if(nested) {
    unsigned chained=0;
    for(const auto& close:derived.closes)if(close.continuation) {
      ++chained;
      Check(close.entries.front().entry.pending.kind==PendingCompletionKind::kReturn);
      bool port=false;
      for(const auto& leaving:close.relation.leaving)if(const auto* edge=std::get_if<CompletionEdge>(&leaving.exit)) {
        if(flow.nodes[edge->source.value].successors[edge->successor].target==*close.continuation) {
          Check(leaving.pending==close.entries[leaving.entry].entry.pending);port=true;
        }
      }
      Check(port);
      // The next close's real entry has the SAME correlated return, not a
      // separately collected payload/target set or a fabricated PC boundary.
      bool matched=false;
      for(const auto& outer:derived.closes)if(outer.protocol!=close.protocol)
        for(const auto& entry:outer.entries)
          if(flow.nodes[entry.entry.edge.source.value].successors[entry.entry.edge.successor].target==*close.continuation) {
            Check(entry.entry.pending==close.entries.front().entry.pending);matched=true;
          }
      Check(matched);
    }
    Check(chained>0);
    const auto proved=AnalyzeCloseCompletion(body,file.header.profile,graph,input,regions,keys,scan,{},SourceContract::kLanguageSourceV4);
    Check(std::holds_alternative<CloseCompletionSummary>(proved));
    const auto& summary=std::get<CloseCompletionSummary>(proved);
    for(const auto& entry:summary.entries) {
      Check(entry.obligations[static_cast<unsigned>(CloseObligation::kPriority)].state==CloseProofState::kVerified);
      // Entry/priority evidence grants no nested storage or topology ownership.
      Check(entry.obligations[static_cast<unsigned>(CloseObligation::kNested)].state==CloseProofState::kMissing);
    }
    Check(std::holds_alternative<std::uint64_t>(VerifyCloseCompletion(body,file.header.profile,graph,input,regions,keys,scan,summary,{},SourceContract::kLanguageSourceV4)));
    auto forged=summary;forged.regions.erase(forged.regions.begin());
    Check(std::holds_alternative<CloseCompletionFailure>(VerifyCloseCompletion(body,file.header.profile,graph,input,regions,keys,scan,forged,{},SourceContract::kLanguageSourceV4)));

    auto wrong=derived;
    const auto at=std::find_if(wrong.closes.begin(),wrong.closes.end(),[](const auto& c){return c.continuation.has_value();});
    at->continuation=ExceptionNodeId{0};
    Check(std::holds_alternative<CloseEntryFailure>(VerifyCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,wrong)));

  } else if(method.offset.value==423) {
    const auto& breaking=find(105);const auto& returning=find(196);
    Check(breaking.entries.size()==1 && returning.entries.size()==1);
    Check(breaking.entries[0].entry.pending.kind==PendingCompletionKind::kBreak);
    Check(breaking.entries[0].entry.pending.target==CompletionTarget{Node(flow,312+padding,ExceptionPhase::kBefore)});
    Check(breaking.entries[0].entry.edge.source==Node(flow,85+padding,ExceptionPhase::kCommit));
    Check(returning.entries[0].entry.pending.kind==PendingCompletionKind::kReturn);
    Check(std::holds_alternative<CompletionCaller>(returning.entries[0].entry.pending.target));
    Check(returning.entries[0].entry.pending.payload==CompletionPayload{*ssa.Write(Node(flow,61+padding,ExceptionPhase::kCommit))});
    Check(returning.entries[0].origin==Node(flow,248+padding,ExceptionPhase::kBefore));
    Check(breaking.entries[0].origin==Node(flow,155+padding,ExceptionPhase::kCommit));
    Check(breaking.relation.leaving.size()==7 && returning.relation.leaving.size()==7);
    Check(breaking.iteration.loop_header==Node(flow,37+padding,ExceptionPhase::kBefore));
    const auto& rethrowing=find(271);
    Check(rethrowing.entries.size()==6 && rethrowing.relation.leaving.size()==6);
    const std::array sources{139u,146u,150u,230u,237u,241u};
    for(std::size_t e=0;e<sources.size();++e) {
      Check(rethrowing.entries[e].entry.edge.source==Node(flow,sources[e]+padding,ExceptionPhase::kBefore));
      const auto& leaving=rethrowing.relation.leaving[e];
      Check(leaving.entry==e && leaving.pending==rethrowing.entries[e].entry.pending);
      Check(leaving.exit==CompletionExit{CompletionCallerExit{Node(flow,307+padding,ExceptionPhase::kBefore)}});
    }
  } else {
    const auto& throwing=find(96);Check(throwing.entries.size()==1);
    const auto& e=throwing.entries[0];
    Check(e.entry.pending.kind==PendingCompletionKind::kThrow);
    Check(e.entry.edge.source==Node(flow,72+padding,ExceptionPhase::kBefore));
    Check(e.catcher==Node(flow,76+padding,ExceptionPhase::kCatch));
    Check(e.entry.pending.payload==CompletionPayload{ssa.Reads(Node(flow,72+padding,ExceptionPhase::kBefore))[0].value});
    Check(!throwing.iteration.loop_header);
    Check(throwing.iteration.advance==Node(flow,39+padding,ExceptionPhase::kBefore));
    Check(throwing.relation.leaving.size()==2);
  }
  auto bad=derived;
  const auto reject=[&](const CloseEntries& value) {
    Check(std::holds_alternative<CloseEntryFailure>(VerifyCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,value)));
  };
  bad.closes[0].entries[0].entry.edge={Node(flow,padding,ExceptionPhase::kBefore),0};reject(bad);
  bad=derived;bad.closes[0].entries[0].entry.pending.target=ExceptionNodeId{0};reject(bad);
  bad=derived;bad.closes[0].entries[0].entry.pending.payload=CompletionEntryPayload{99};reject(bad);
  bad=derived;bad.closes[0].entries.clear();reject(bad);
  bad=derived;bad.closes[0].iteration.advance={0};reject(bad);
  bad=derived;bad.closes[0].iteration.value=ExceptionNodeId{0};reject(bad);
  bad=derived;bad.closes[0].relation.leaving.clear();reject(bad);
  Check(std::get<CloseEntryFailure>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{0})).kind==CloseEntryError::kResourceLimit);
  Check(std::get<CloseEntryFailure>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{64*1024*1024,0})).kind==CloseEntryError::kWorkLimit);
  std::size_t low=0,high=derived.peak_bytes;
  while(low<high) {
    const auto mid=low+(high-low)/2;
    if(std::holds_alternative<CloseEntries>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{mid})))high=mid;
    else low=mid+1;
  }
  Check(std::holds_alternative<CloseEntries>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{low})));
  Check(low && std::get<CloseEntryFailure>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{low-1})).kind==CloseEntryError::kResourceLimit);
  Check(std::holds_alternative<CloseEntries>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{64*1024*1024,derived.work})));
  Check(std::get<CloseEntryFailure>(DeriveCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,{64*1024*1024,derived.work-1})).kind==CloseEntryError::kWorkLimit);
  const auto verified_work=std::get<std::uint64_t>(VerifyCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,derived));
  Check(std::holds_alternative<std::uint64_t>(VerifyCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,derived,{64*1024*1024,verified_work})));
  Check(std::get<CloseEntryFailure>(VerifyCloseEntries(body,file.header.profile,graph,input,regions,keys,scan,derived,{64*1024*1024,verified_work-1})).kind==CloseEntryError::kWorkLimit);
  std::cout<<"method="<<method.offset.value<<" padding="<<padding<<" closes="<<derived.closes.size()
    <<" exact-bytes="<<low<<" work="<<derived.work<<" conservative-sentinel-values\n";
}
}
int main(int argc,char** argv) {
  Check(argc==2 || argc==3);
  const auto file=std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash=std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  if(argc==3) {
    const auto entity=std::stoul(argv[2]);
    const auto method=std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& m){return m.offset.value==entity;});
    Check(method!=index.methods.end());
    for(unsigned n:{0,200,201,400,800})Method({*file,header,index},*method,n,Mutation::kNone,true);
    Method({*file,header,index},*method,0,Mutation::kNone,true,1);
    return 0;
  }
  unsigned tested=0;
  for(const auto& method:index.methods) if(method.offset.value==423 || method.offset.value==451) {
    ++tested;
    Method({*file,header,index},method);
    Method({*file,header,index},method,0,Mutation::kRegisters);
    Method({*file,header,index},method,0,Mutation::kSplitDone);
    for(auto mutation:{Mutation::kInitiallyClosed,Mutation::kReset,Mutation::kReceiver,Mutation::kInvertedDone,Mutation::kCycleExit})Method({*file,header,index},method,0,mutation);
    if(method.offset.value==451) for(unsigned n:{200,201,400,800}) Method({*file,header,index},method,n);
  }
  Check(tested==2);
}
