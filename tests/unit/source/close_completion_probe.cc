// Read-only standalone certificate diagnostic. No production admission hook.
#include "../../../src/source/close_completion.h"
#include "../../../src/source/exception_control.h"
#include "../../../src/source/for_of_evaluation.h"
#include "../../../src/source/exception_forwarding.h"
#include "abcd/source/async_completion.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/analysis/exception_captures.h"
#include "abcd/analysis/exception_modules.h"
#include "abcd/abc/call_type.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <algorithm>
#include <type_traits>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct Refused {};
template<class T,class...U>T Take(std::variant<U...> value,const char* stage) {
  if(auto* p=std::get_if<T>(&value))return std::move(*p);
  std::cout<<"stage="<<stage<<" alternative="<<value.index();
  std::visit([](const auto& f){
    if constexpr(std::is_enum_v<std::decay_t<decltype(f)>>)std::cout<<" enum="<<unsigned(f);
    if constexpr(requires{f.kind;})std::cout<<" kind="<<unsigned(f.kind);
    if constexpr(requires{f.region.has_value();})if(f.region)std::cout<<" region="<<f.region->value;
    if constexpr(requires{f.operation.has_value();})if(f.operation)std::cout<<" op="<<f.operation->value;
    if constexpr(requires{f.cause.kind;})std::cout<<" cause="<<unsigned(f.cause.kind);
    if constexpr(requires{f.cause.pc.value;})std::cout<<" cause-pc="<<f.cause.pc.value;
    if constexpr(requires{f.relation.has_value();})if(f.relation)std::cout<<" relation="<<unsigned(f.relation->kind);
    if constexpr(requires{f.node.has_value();})if(f.node)std::cout<<" node="<<f.node->value;
    if constexpr(requires{f.pc;}) {if constexpr(requires{f.pc.value;})std::cout<<" pc="<<f.pc.value;else if(f.pc)std::cout<<" pc="<<f.pc->value;}
  },value);std::cout<<'\n';throw Refused{};
}
int main(int argc,char**argv) {
  test::Check(argc>=3);
  try {
    auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]),"file");
    auto hash=Take<corpus::FileFingerprint>(file->Fingerprint(),"fingerprint");
    auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size),"header");
    auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header),"metadata");
    if(std::string_view(argv[2])=="--acquisitions") {
      std::size_t methods=0,refused=0,acquisitions=0;
      for(const auto& m:index.methods)if(m.code) {
        ++methods;
        auto decoded=abc::DecodeMethodBody(*file,header.profile,*m.code);
        if(!std::holds_alternative<abc::MethodBody>(decoded)) {
          ++refused;std::cout<<"inventory-decode-refusal method="<<m.offset.value<<'\n';continue;
        }
        if(const auto* b=std::get_if<abc::MethodBody>(&decoded))for(const auto& i:b->instructions) {
          const auto lifted=LiftIteratorInstruction(i.decoded,std::uint64_t{m.code->registers}+m.code->arguments);
          if(const auto* a=std::get_if<AcquireIterator>(&lifted);a && a->mode==IteratorMode::kSync) {
            ++acquisitions;
            std::cout<<"acquisition method="<<m.offset.value<<" record="<<m.owner_offset.value<<" pc="<<i.offset.value<<" name="<<m.name<<'\n';
          }
        }
      }
      std::cout<<"inventory methods="<<methods<<" decode-refusals="<<refused<<" acquisitions="<<acquisitions<<'\n';
      return refused?1:0;
    }
    const auto entity=std::stoul(argv[2]);
    const auto method=std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& m){return m.offset.value==entity;});
    test::Check(method!=index.methods.end() && method->code);
    const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,method->annotations),"calltype");
    const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(method->code->registers,method->code->arguments,abc::DecodeCallFlags(call.value)),"frame");
    auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code),"body");
    std::vector<abc::CodeOffset> origins;std::vector<AwaitSite> awaits;
    if((argc>4 && std::string_view(argv[4])=="--async") || (argc>3 && std::string_view(argv[3])=="--async"))
      body=Take<abc::MethodBody>(ProveAsyncCompletion(body,frame,64*1024*1024,&origins,&awaits,header.profile,ResourceFailureScope::kExcluded),"async");
    auto flow=Take<ExceptionFlow>(BuildExceptionFlow(body,header.profile,frame.registers,{},awaits,&frame),"flow");
    for(std::size_t i=0;i<origins.size();++i)flow.instructions[i].origin=origins[i];
    IdfQuota quota{kDefaultSemanticWork};
    auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota),"ssa");
    std::vector<EntryValueFact> entry;
    for(std::uint32_t v=0;v<ssa.Values().size();++v)if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=method->code->registers)
        entry.push_back({{v},ValueKinds::Language()});
    auto env=Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow),"environments");
    // These are the SAME conditional premises as PhasedBody, not filled-in
    // Language facts: the ordinary source owner must still close captures and
    // transitive closure writes. This probe never publishes source/ownership.
    auto locals=Take<ExceptionLocals>(ProveExceptionLocals(flow,env,{}, {},ClosureWrites::kLanguage),"locals");
    auto captures=Take<ExceptionCaptures>(PlanExceptionCaptures(flow,env),"captures");
    auto modules=Take<ExceptionModules>(ProveExceptionModules(flow,ssa),"modules");
    std::vector<ExceptionLoadFact> loads=locals.loads;
    loads.insert(loads.end(),captures.loads.begin(),captures.loads.end());
    loads.insert(loads.end(),modules.loads.begin(),modules.loads.end());
    auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entry,quota,{},PendingExceptionValues::kLanguage,loads, {},CallResultValues::kLanguage,FrameReceiver::kUnknown,Arithmetic(SourceContract::kLanguageSourceV4)),"values");
    auto checked=VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport);
    if(const auto* failed=std::get_if<ExceptionInputFailure>(&checked);failed && failed->kind==ExceptionInputError::kInternalValue) {
      values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entry,quota,{},PendingExceptionValues::kLanguage,loads, {},CallResultValues::kLanguage,FrameReceiver::kUnknown,Arithmetic(SourceContract::kLanguageSourceV4),ExceptionThrowRefinement::kGuardedNonHole),"refined-values");
      checked=VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport);
    }
    if(const auto* f=std::get_if<ExceptionInputFailure>(&checked)) {
      std::cout<<"input-failure kind="<<unsigned(f->kind)<<" working-pc="<<f->pc.value;
      if(f->node) {
        std::cout<<" node="<<f->node->value;
        const auto& site=flow.instructions[flow.nodes[f->node->value].instruction.value];
        if(site.origin)std::cout<<" original-pc="<<site.origin->value;
      }
      if(f->value) {
        std::cout<<" value="<<f->value->value;
        const auto& origin=ssa.Values()[f->value->value].origin;
        if(const auto* site=std::get_if<InstructionValue>(&origin))std::cout<<" defined-pc="<<flow.instructions[flow.nodes[site->instruction.value].instruction.value].pc.value;
        if(std::holds_alternative<PhiValue>(origin))std::cout<<" phi=1";
        for(auto kind:{VmValueKind::kLanguage,VmValueKind::kHole,VmValueKind::kEnvironment,VmValueKind::kOtherInternal,VmValueKind::kPropertyEnumerator})
          if(values.Facts()[f->value->value].Contains(kind))std::cout<<" category="<<unsigned(kind);
      }
      std::cout<<'\n';
    }
    auto proof=Take<ExceptionInputProof>(std::move(checked),"inputs");
    PhasedValueInput input{flow,ssa,env,values,proof,frame};
    auto graph=Take<ValueProgram>(ImportPhasedValues(input),"import");
    auto plan=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input),"plan");
    if(argc>3 && std::string_view(argv[3])=="--boundary")for(std::uint32_t t=0;t<plan.layout.regions.size();++t) {
      const auto& r=plan.layout.regions[t];
      std::cout<<"plan-try="<<t<<" body="<<flow.instructions[r.body.begin.value].pc.value<<" handler="<<flow.instructions[r.handler.begin.value].pc.value<<" inactive="<<r.inactive.has_value()<<'\n';
    }
    auto keys=Take<IteratorKeys>(ResolveIteratorKeys({*file,header,index},method->offset,graph),"keys");
    auto scan=Take<IteratorProtocolScan>(ScanIteratorProtocols(graph,input,plan,keys),"scan");
    std::cout<<"method="<<entity<<" acquisitions="<<scan.candidates.size()<<" regions="<<plan.regions.size()<<'\n';
    if(argc>3 && (std::string_view(argv[3])=="--members" || std::string_view(argv[3])=="--boundary"))for(const auto& r:scan.regions) {
      const auto& x=plan.layout.regions[r.region.value];
      std::cout<<"try="<<r.region.value<<" protocol="<<r.protocol.value<<" role="<<unsigned(r.role)<<" body="<<flow.instructions[x.body.begin.value].pc.value<<" handler="<<flow.instructions[x.handler.begin.value].pc.value<<" body-end-site="<<x.body.end.value<<" handler-end-site="<<x.handler.end.value<<" read="<<r.read.working_pc.value<<'\n';
    }
    if(argc>3 && std::string_view(argv[3])=="--boundary") {
      auto derived=DeriveCloseEntries(body,header.profile,graph,input,plan,keys,scan);
      if(const auto* f=std::get_if<CloseEntryFailure>(&derived);f && f->node) {
        const auto& site=flow.instructions[flow.nodes[f->node->value].instruction.value];
        std::cout<<"entry-witness original="<<site.origin.value_or(site.pc).value<<" working="<<site.pc.value<<'\n';
      }
      auto entries=Take<CloseEntries>(std::move(derived),"entries");
      const auto pc=[&](ExceptionNodeId n){return flow.instructions[flow.nodes[n.value].instruction.value].pc.value;};
      std::vector<bool> live(flow.nodes.size());std::vector<ExceptionNodeId> pending;
      live[0]=true;pending.push_back({0});
      for(std::size_t i=0;i<pending.size();++i)for(std::uint8_t e=0;e<flow.nodes[pending[i].value].successor_count;++e) {
        const auto n=flow.nodes[pending[i].value].successors[e].target;if(!live[n.value]){live[n.value]=true;pending.push_back(n);}
      }
      for(std::uint32_t n=0;n<flow.nodes.size();++n) {
        const auto& node=flow.nodes[n];if(node.phase!=ExceptionPhase::kCommit || !std::holds_alternative<ObjectCheck>(flow.instructions[node.instruction.value].operation))continue;
        std::cout<<" resultcheck="<<pc({n})<<" reachable="<<live[n];
        for(std::uint8_t e=0;e<node.successor_count;++e)std::cout<<" ->"<<pc(node.successors[e].target);
        std::cout<<'\n';
      }

      for(const auto& c:entries.closes) {
        std::cout<<"BOUNDARY read="<<flow.instructions[flow.nodes[graph.phases[graph.ops[c.read.value].block.value].input.value].instruction.value].pc.value<<" exhausted="<<pc(c.iteration.exhausted.source)<<"->"<<pc(flow.nodes[c.iteration.exhausted.source.value].successors[c.iteration.exhausted.successor].target)<<'\n';
        if(c.continuation)std::cout<<" next-close="<<pc(*c.continuation)<<'\n';
        for(const auto& e:c.entries) {
          std::cout<<" derived-entry="<<pc(e.entry.edge.source)<<":"<<unsigned(e.entry.edge.successor)
              <<"->"<<pc(flow.nodes[e.entry.edge.source.value].successors[e.entry.edge.successor].target)
              <<" completion="<<unsigned(e.entry.pending.kind);
          if(const auto* value=std::get_if<SsaValueId>(&e.entry.pending.payload))std::cout<<" payload=V"<<value->value;
          std::cout<<'\n';
        }
        for(const auto& e:c.exits)std::visit([&](const auto& x){
          std::cout<<" exit="<<pc(x.source)<<" phase="<<unsigned(flow.nodes[x.source.value].phase);
          if constexpr(requires{x.successor;})std::cout<<"->"<<pc(flow.nodes[x.source.value].successors[x.successor].target);
          std::cout<<'\n';
        },e);
      }
    }
    auto result=AnalyzeCloseCompletion(body,header.profile,graph,input,plan,keys,scan,{},SourceContract::kLanguageSourceV4);
    if(auto* f=std::get_if<CloseCompletionFailure>(&result)) {
      std::cout<<"failure="<<unsigned(f->kind);
      if(f->derivation) {
        std::cout<<" derivation="<<unsigned(f->derivation->kind);
        if(f->derivation->relation)std::cout<<" relation="<<unsigned(f->derivation->relation->kind);
      }
      const auto n=f->node?f->node:(f->derivation?f->derivation->node:std::nullopt);
      if(n) {
        const auto& instruction=flow.instructions[flow.nodes[n->value].instruction.value];
        std::cout<<" node="<<n->value<<" pc="<<instruction.origin.value_or(instruction.pc).value<<" working-pc="<<instruction.pc.value;
        if(instruction.origin)std::cout<<" original-pc="<<instruction.origin->value;
        if(f->derivation && f->derivation->relation) {
          RawEventArena arena({});RawEventGraph raw(body,arena);
          const auto dispatch=raw.Select(header.profile);raw.Allocate();raw.Connect(dispatch,header.profile,frame.registers);
          const auto& source=flow.nodes[n->value];
          const auto phase=source.phase==ExceptionPhase::kBefore?0U:source.phase==ExceptionPhase::kCommit?1U:2U;
          const auto& event=raw.Nodes()[source.instruction.value*RawEventGraph::kPhases+phase];
          std::cout<<" raw-successors="<<unsigned(event.count)<<" flow-successors="<<unsigned(source.successor_count)
            <<" raw-unknown="<<event.unknown;
          for(std::uint8_t i=0;i<source.successor_count;++i)std::cout<<" flow-target="<<flow.instructions[flow.nodes[source.successors[i].target.value].instruction.value].pc.value;
          for(std::uint8_t i=0;i<event.count;++i)std::cout<<" raw-target="<<raw.Pc(event.next[i]).value;
        }
      }
      std::cout<<'\n';return 0;
    }
    if(argc>3 && (std::string_view(argv[3])=="--control" || std::string_view(argv[3])=="--evaluation")) {
      auto sealed=VerifyProtocolRegionOwnership(body,header.profile,graph,input,plan,
          keys,scan,std::get<CloseCompletionSummary>(std::move(result)),{},SourceContract::kLanguageSourceV4);
      if(const auto* f=std::get_if<CloseCompletionFailure>(&sealed);f && f->region) {
        const auto& r=plan.layout.regions[f->region->value];
        const auto& b=flow.instructions[r.body.begin.value];const auto& h=flow.instructions[r.handler.begin.value];
        std::cout<<"unowned-try="<<f->region->value<<" body="<<b.origin.value_or(b.pc).value<<" working-body="<<b.pc.value
          <<" handler="<<h.origin.value_or(h.pc).value<<" working-handler="<<h.pc.value<<'\n';
        const auto forwarded=AnalyzeExceptionForwarding(graph);
        if(const auto* proof=std::get_if<ExceptionForwarding>(&forwarded)) {
          const auto handler=plan.regions[f->region->value].handler;
          std::cout<<"unowned-handler-forwarding="<<proof->blocks[handler.value].has_value()<<'\n';
        }
      }
      if(const auto* f=std::get_if<CloseCompletionFailure>(&sealed);f && f->read && f->read->value<graph.ops.size()) {
        const auto& op=graph.ops[f->read->value];const auto& origin=graph.origins[op.origin.value];
        const auto& instruction=flow.instructions[graph.phases[op.block.value].instruction.value];
        std::cout<<"ownership-read="<<f->read->value<<" pc="<<instruction.origin.value_or(instruction.pc).value<<" working-pc="<<origin.pc.value;
        if(f->entry)std::cout<<" entry="<<*f->entry;
        if(f->protocol)std::cout<<" protocol="<<f->protocol->value;
        std::cout<<'\n';
      }
      auto owner=Take<VerifiedProtocolRegionOwnership>(std::move(sealed),"ownership");
      auto control=Take<ExceptionControl>(BuildExceptionControl(body,graph,input,plan,{},&owner),"control");
      Take<std::uint64_t>(VerifyExceptionControl(body,graph,input,plan,control,{},&owner),"control-verify");
      if(std::string_view(argv[3])=="--evaluation") {
        const ForOfEvaluationInput source{body,input,plan,control,owner,SourceContract::kLanguageSourceV4};
        auto evaluation=Take<ForOfEvaluationPlan>(BuildForOfEvaluationPlan(graph,source),"evaluation");
        Take<std::uint64_t>(VerifyForOfEvaluationPlan(graph,source,evaluation),"verify-evaluation");
        std::cout<<"evaluation-verified\n";
      }
      std::cout<<"protocol-control units="<<control.units.size()<<" loops="<<control.skeleton.loops.size()<<'\n';return 0;
    }
    const auto& summary=std::get<CloseCompletionSummary>(result);
    Take<std::uint64_t>(VerifyCloseCompletion(body,header.profile,graph,input,plan,keys,scan,summary,{},SourceContract::kLanguageSourceV4),"verify");
    for(const auto& e:summary.entries) {
      std::cout<<"protocol="<<e.protocol.value<<" read="<<flow.instructions[flow.nodes[graph.phases[graph.ops[e.read.value].block.value].input.value].instruction.value].pc.value<<" entry="<<flow.instructions[flow.nodes[e.source.source.value].instruction.value].pc.value<<" completion="<<unsigned(e.pending.kind);
      if(const auto* value=std::get_if<SsaValueId>(&e.pending.payload))std::cout<<" payload=V"<<value->value;
      if(const auto* target=std::get_if<ExceptionNodeId>(&e.pending.target))std::cout<<" target="<<flow.instructions[flow.nodes[target->value].instruction.value].pc.value;
      for(std::size_t o=0;o<e.obligations.size();++o) {
        const auto& x=e.obligations[o];std::cout<<" obligation"<<o<<'='<<unsigned(x.state);
        if(x.missing) {std::cout<<':'<<unsigned(x.missing->kind);if(x.missing->region)std::cout<<"[try="<<x.missing->region->value<<"]";if(x.missing->ownership_gap)std::cout<<"[ownership-gap="<<unsigned(*x.missing->ownership_gap)<<']';if(x.missing->node)std::cout<<'@'<<flow.instructions[flow.nodes[x.missing->node->value].instruction.value].pc.value;if(x.missing->edge){const auto edge=*x.missing->edge;const auto& a=flow.nodes[edge.source.value];const auto& b=flow.nodes[a.successors[edge.successor].target.value];std::cout<<" edge="<<flow.instructions[a.instruction.value].pc.value<<":"<<unsigned(a.phase)<<":"<<unsigned(edge.successor)<<"->"<<flow.instructions[b.instruction.value].pc.value<<":"<<unsigned(b.phase);}}
      }std::cout<<'\n';
    }
    if(argc>3 && std::string_view(argv[3])=="--boundary")for(const auto& r:summary.regions)if(r.boundary) {
      std::cout<<" boundary-read="<<flow.instructions[flow.nodes[graph.phases[graph.ops[r.read.value].block.value].input.value].instruction.value].pc.value
        <<" entry-proof="<<unsigned(r.boundary->entry_coverage)<<" port-proof="<<unsigned(r.boundary->port_coverage)
        <<" private-proof="<<unsigned(r.boundary->private_state)<<" region-proof="<<unsigned(r.boundary->region_coverage)
        <<" ports="<<r.boundary->ports.size()<<" private="<<r.boundary->private_slots.size()<<" dead-entry-edges="<<r.boundary->unreachable_entries.size()<<'\n';
      for(const auto& split:r.boundary->protected_ranges) {
        std::cout<<" split-try="<<split.region.value<<" role="<<unsigned(split.role)<<" ordinary="<<split.ordinary.size()
          <<" transport="<<split.transport.size()<<" dispatch="<<split.dispatch.size()<<" exits="<<split.exits.size();
        if(split.missing) {
          std::cout<<" gap="<<unsigned(*split.missing->ownership_gap);
          if(split.missing->node)std::cout<<" pc="<<flow.instructions[flow.nodes[split.missing->node->value].instruction.value].pc.value;
        }
        std::cout<<'\n';
      }
      for(const auto edge:r.boundary->unreachable_entries) {
        const auto& a=flow.nodes[edge.source.value];const auto& b=flow.nodes[a.successors[edge.successor].target.value];
        std::cout<<" dead-edge="<<flow.instructions[a.instruction.value].pc.value<<":"<<unsigned(a.phase)<<"->"<<flow.instructions[b.instruction.value].pc.value<<":"<<unsigned(b.phase)<<'\n';
      }
    }
    if(argc>3 && std::string_view(argv[3])=="--members")for(const auto& r:summary.regions) {
      std::cout<<"REGION read="<<flow.instructions[flow.nodes[graph.phases[graph.ops[r.read.value].block.value].input.value].instruction.value].pc.value<<'\n';
      for(const auto& m:r.members)std::cout<<" member="<<m.node.value<<" pc="<<flow.instructions[flow.nodes[m.node.value].instruction.value].pc.value<<" phase="<<unsigned(flow.nodes[m.node.value].phase)<<" role="<<unsigned(m.role)<<'\n';
    }
  }catch(const Refused&){return 0;}
}
