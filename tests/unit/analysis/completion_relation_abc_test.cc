#include "abcd/analysis/completion_relation.h"
#include "abcd/corpus/abc_file.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
using test::Check;
namespace {
struct Original {
  const abc::MethodBody& body;
  const ExceptionFlow& flow;
  const ExceptionSsa& ssa;
  ExceptionNodeId Node(unsigned pc,ExceptionPhase phase) const {
    for(std::uint32_t n=0;n<flow.nodes.size();++n)
      if(flow.nodes[n].phase==phase && flow.instructions[flow.nodes[n].instruction.value].pc.value==pc) return {n};
    Check(false);return {};
  }
  CompletionEdge Into(unsigned pc,unsigned destination,ExceptionPhase phase=ExceptionPhase::kCommit) const {
    const auto source=Node(pc,phase);
    for(std::uint8_t e=0;e<flow.nodes[source.value].successor_count;++e) {
      const auto& target=flow.nodes[flow.nodes[source.value].successors[e].target.value];
      if(flow.instructions[target.instruction.value].pc.value==destination) return {source,e};
    }
    Check(false);return {};
  }
  std::vector<CompletionExit> Cut(unsigned begin,unsigned end) const {
    std::vector<CompletionExit> exits;
    for(std::uint32_t n=0;n<flow.nodes.size();++n) {
      const auto& node=flow.nodes[n];const auto pc=flow.instructions[node.instruction.value].pc.value;
      if(pc<begin || pc>=end) continue;
      for(std::uint8_t e=0;e<node.successor_count;++e) {
        const auto target=flow.instructions[flow.nodes[node.successors[e].target.value].instruction.value].pc.value;
        if(target<begin || target>=end) exits.push_back(CompletionEdge{{n},e});
      }
      if((node.exception && !node.exception->target) ||
          (node.phase==ExceptionPhase::kCommit && std::holds_alternative<ReturnOperation>(flow.instructions[node.instruction.value].operation)))
        exits.push_back(CompletionCallerExit{{n}});
    }
    return exits;
  }
};
void Entry(const abc::MethodBody& body,abc::ProfileId profile,const abc::FrameLayout& frame,
    const ExceptionFlow& flow,const ExceptionSsa& ssa,abc::EntityOffset method,
    CompletionEntry entry,unsigned begin,unsigned end) {
  const Original original{body,flow,ssa};const std::array entries{entry};const auto cuts=original.Cut(begin,end);
  auto analyzed=AnalyzeCompletionRelation(body,profile,frame.registers,flow,ssa,entries,cuts);
  if(auto* f=std::get_if<CompletionRelationFailure>(&analyzed)) {
    std::cerr<<"method "<<method.value<<" relation "<<unsigned(f->kind)<<" node "<<(f->node?f->node->value:UINT32_MAX)<<'\n';Check(false);
  }
  const auto& relation=std::get<CompletionRelation>(analyzed);
  Check(relation.arrivals.size()==1 && !relation.leaving.empty());
  Check(std::holds_alternative<CompletionRelationCost>(VerifyCompletionRelation(body,profile,frame.registers,flow,ssa,entries,cuts,relation)));
  // Independent fixed-fixture oracle for EVERY raw exit tuple. This is the
  // raw may relation: the unknown Hole comparison also keeps a Hole throw.
  // These extra possibilities must not be advertised as native-close proof.
  std::vector<CompletionLeaving> expected;
  const auto caller=[&](unsigned pc) {
    expected.push_back({0,CompletionCallerExit{original.Node(pc,ExceptionPhase::kBefore)},entry.pending});
  };
  if(method.value==451) { caller(123);caller(132); }
  else {
    const bool returning=begin==178;
    const unsigned rethrow=returning?237:146;
    const unsigned compare=returning?230:139;
    const unsigned check=returning?241:150;
    const unsigned hole=returning?191:100;
    for(const auto origin: returning?std::array{196u,205u,212u}:std::array{105u,114u,121u})
      expected.push_back({0,original.Into(rethrow,251,ExceptionPhase::kBefore),
        {PendingCompletionKind::kThrow,CompletionExceptionValue{original.Node(origin,ExceptionPhase::kBefore)},
          original.Node(251,ExceptionPhase::kCatch)}});
    expected.push_back({0,original.Into(rethrow,251,ExceptionPhase::kBefore),
      {PendingCompletionKind::kThrow,*ssa.Write(original.Node(hole,ExceptionPhase::kCommit)),
        original.Node(251,ExceptionPhase::kCatch)}});
    for(const auto pc:{compare,check})
      expected.push_back({0,original.Into(pc,251,ExceptionPhase::kBefore),
        {PendingCompletionKind::kThrow,CompletionExceptionValue{original.Node(pc,ExceptionPhase::kBefore)},
          original.Node(251,ExceptionPhase::kCatch)}});
    if(returning) expected.push_back({0,CompletionCallerExit{original.Node(248,ExceptionPhase::kCommit)},entry.pending});
    else expected.push_back({0,original.Into(155,312),entry.pending});
  }
  std::sort(expected.begin(),expected.end());Check(relation.leaving==expected);
  auto bad=relation;bad.leaving[0].pending.payload=CompletionEntryPayload{77};
  Check(std::holds_alternative<CompletionRelationFailure>(VerifyCompletionRelation(body,profile,frame.registers,flow,ssa,entries,cuts,bad)));
  std::cout<<"method="<<method.value<<" close-entry="<<begin<<" states="<<relation.states<<" exits="<<relation.leaving.size()<<'\n';
}
}
int main(int argc,char** argv) {
  Check(argc==2);
  const auto file=std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto fingerprint=std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=std::get<abc::Header>(abc::ParseHeader(std::span(fingerprint.prefix).first(fingerprint.prefix_size),fingerprint.size));
  const auto metadata=std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  unsigned tested=0;
  for(const auto& method:metadata.methods) {
    if(method.offset.value!=423 && method.offset.value!=451) continue;
    const auto body=std::get<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method.code));
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers,method.code->arguments,{}));
    const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,header.profile,frame.registers,{}, {}, &frame));
    IdfQuota quota{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota));
    const Original original{body,flow,ssa};
    if(method.offset.value==451) {
      const auto thrown=ssa.Reads(original.Node(72,ExceptionPhase::kBefore))[0].value;
      Entry(body,header.profile,frame,flow,ssa,method.offset,
        {original.Into(72,76,ExceptionPhase::kBefore),{PendingCompletionKind::kThrow,thrown,CompletionCaller{}}},76,134);
    } else {
      Entry(body,header.profile,frame,flow,ssa,method.offset,
        {original.Into(85,87),{PendingCompletionKind::kBreak,{},original.Node(312,ExceptionPhase::kBefore)}},87,158);
      const auto returned=ssa.Reads(original.Node(172,ExceptionPhase::kBefore))[0].value;
      Entry(body,header.profile,frame,flow,ssa,method.offset,
        {original.Into(176,178),{PendingCompletionKind::kReturn,returned,CompletionCaller{}}},178,249);
    }
    ++tested;
  }
  Check(tested==2);
}
