// Read-only standalone certificate diagnostic. No production admission hook.
#include "../../../src/source/for_of_iteration.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/for_of_events.h"
#ifdef ABCD_TEST_FOR_OF_DRAFT
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_labels.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/for_of_emission.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_constants.h"
#include "abcd/source/tree_printer.h"
#include <fstream>
#endif
#include "../../../src/source/for_of_evaluation.h"
#ifdef ABCD_TEST_FOR_OF_CELLS
#include "../../../src/source/cell_lifetime.h"
#endif
#include "../../../src/analysis/value_edits.h"
#include "../../../src/source/exception_control.h"
#include "../../../src/source/exception_topology.h"
#include "abcd/source/async_completion.h"
#include "abcd/analysis/raw_event_graph.h"
#include "../../../src/analysis/normalized_raw_semantics.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/analysis/exception_captures.h"
#include "abcd/analysis/exception_modules.h"
#include "abcd/abc/call_type.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <algorithm>
#include <map>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
#ifdef ABCD_TEST_ENUMERATION_COMPOSITION
#include "enumeration_composition_checks.h"
#endif
struct Refused {};
#ifdef ABCD_TEST_FOR_OF_DRAFT
std::string draft_output;
#endif
template<class T,class...U>T Take(std::variant<U...> value,const char* stage) {
  if(auto* p=std::get_if<T>(&value))return std::move(*p);
  std::cout<<"stage="<<stage<<" alternative="<<value.index();
  std::visit([](const auto& f){
    if constexpr(std::is_enum_v<std::decay_t<decltype(f)>>)std::cout<<" enum="<<unsigned(f);
    if constexpr(requires{f.kind;})std::cout<<" kind="<<unsigned(f.kind);
    if constexpr(requires{f.pc;}) {if constexpr(requires{f.pc.value;})std::cout<<" pc="<<f.pc.value;else if(f.pc)std::cout<<" pc="<<f.pc->value;}
  },value);std::cout<<'\n';throw Refused{};
}
void Case(const char* path,unsigned padding,unsigned entity=451,unsigned shift=0,bool split=false,bool expected_gap=false,bool protocol_only=false) {
  try {
    auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path),"file");
    auto hash=Take<corpus::FileFingerprint>(file->Fingerprint(),"fingerprint");
    auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size),"header");
    auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header),"metadata");

    const auto method=std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& m){return m.offset.value==entity;});
    test::Check(method!=index.methods.end() && method->code);
    const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,method->annotations),"calltype");
    const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(method->code->registers+shift,method->code->arguments,abc::DecodeCallFlags(call.value)),"frame");
    auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code),"body");
    if(shift)for(auto& i:body.instructions)for(auto& o:std::span(i.decoded.operands).first(i.decoded.operand_count))if(o.kind==abc::OperandKind::kRegister){test::Check(o.raw_bits+shift<(std::uint64_t{1}<<o.width_bits));o.raw_bits+=shift;}
    std::vector<abc::CodeOffset> origins;std::vector<AwaitSite> awaits;
#ifdef ABCD_TEST_FOR_OF_ASYNC
    // Exercise layout variation in the graph being certified, after the
    // independent owner proves/removes its private resume protocol.
    if(split)body=Take<abc::MethodBody>(ProveAsyncCompletion(body,frame,SemanticBudget{}.bytes,&origins,&awaits,header.profile,ResourceFailureScope::kExcluded),"async");
#endif
    if(split){
      std::map<std::uint32_t,std::uint32_t> positions;std::uint32_t end=0;
      for(const auto& i:body.instructions){positions[i.offset.value]=end;end+=i.decoded.size+1;}
      const auto old_end=body.instructions.back().offset.value+body.instructions.back().decoded.size;positions[old_end]=end;
      auto nop=test::Body({0xd5}).instructions.front();std::vector<abc::BodyInstruction> changed;
      std::vector<abc::CodeOffset> mapped_origins;std::size_t original_index=0;
      for(auto i:body.instructions){
        if(!origins.empty()){mapped_origins.push_back(origins[original_index]);mapped_origins.push_back(origins[original_index++]);}
        const auto at=positions.at(i.offset.value);nop.offset={at};changed.push_back(nop);i.offset={at+1};if(i.branch_target){i.branch_target=abc::CodeOffset{positions.at(i.branch_target->value)};if(i.decoded.branch_delta)i.decoded.branch_delta=std::int64_t{i.branch_target->value}-i.offset.value;}changed.push_back(i);}
      for(auto& r:body.tries){const auto first=positions.at(r.offset.value),last=positions.at(r.offset.value+r.length);r.offset={first};r.length=last-first;}
      for(auto& h:body.handlers){if(h.length)h.length=positions.at(h.offset.value+h.length)-positions.at(h.offset.value);h.offset={positions.at(h.offset.value)};}
      for(auto& site:awaits){site.pc={positions.at(site.pc.value)+1};site.mode_pc={positions.at(site.mode_pc.value)+1};}
      if(!origins.empty())origins=std::move(mapped_origins);
      body.instructions=std::move(changed);
    }
    if(padding) {
      const auto nop=test::Body({0xd5}).instructions.front();
      for(auto& i:body.instructions){i.offset.value+=padding;if(i.branch_target)i.branch_target->value+=padding;}
      for(auto& t:body.tries)t.offset.value+=padding;
      for(auto& h:body.handlers)h.offset.value+=padding;
      body.instructions.insert(body.instructions.begin(),padding,nop);
      for(unsigned i=0;i<padding;++i)body.instructions[i].offset={i};
    }
#ifdef ABCD_TEST_FOR_OF_ASYNC
    if(!split)body=Take<abc::MethodBody>(ProveAsyncCompletion(body,frame,SemanticBudget{}.bytes,&origins,&awaits,header.profile,ResourceFailureScope::kExcluded),"async");
#endif
    auto flow=Take<ExceptionFlow>(BuildExceptionFlow(body,header.profile,frame.registers,{},awaits,&frame),"flow");
    for(std::size_t i=0;i<origins.size();++i)flow.instructions[i].origin=origins[i];
    IdfQuota quota{kDefaultSemanticWork};
    auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota),"ssa");
    std::vector<EntryValueFact> entry;
    for(std::uint32_t v=0;v<ssa.Values().size();++v)if(std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
      if(const auto* reg=std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);reg && reg->value>=method->code->registers+shift)
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
    if(const auto* f=std::get_if<ExceptionInputFailure>(&checked);f && f->kind==ExceptionInputError::kInternalValue) {
      values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entry,quota,{},PendingExceptionValues::kLanguage,loads, {},CallResultValues::kLanguage,FrameReceiver::kUnknown,Arithmetic(SourceContract::kLanguageSourceV4),ExceptionThrowRefinement::kGuardedNonHole),"refined-values");
      checked=VerifyExceptionInputs(flow,ssa,env,values,kDefaultSemanticWork,nullptr,ExceptionInputDomain::kHoleTransport);
    }
    if(const auto* f=std::get_if<ExceptionInputFailure>(&checked)) {
      std::cout<<"input-failure kind="<<unsigned(f->kind)<<" working-pc="<<f->pc.value;
      if(f->node)std::cout<<" node="<<f->node->value;
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
#ifdef ABCD_TEST_FOR_OF_ASYNC
    test::Check(!flow.awaits.empty());
    const auto stale=[&](ExceptionFlow changed) {
      RawEventArena arena({});RawEventGraph raw(body,arena);
      const auto dispatch=raw.Select(header.profile);raw.Allocate();
      bool refused=false;
      try {analysis::detail::NormalizedRawSemantics semantics(header.profile,frame.registers,changed.awaits);raw.Connect(dispatch,header.profile,frame.registers,nullptr,&semantics);(void)raw.Nodes();}
      catch(const RawEventFailure& f){refused=f.kind==RawEventError::kCorrespondence;}
      test::Check(refused);
    };
    auto moved=flow;moved.awaits[0].pc.value+=1;stale(std::move(moved));
    auto mode=flow;mode.awaits[0].mode_pc.value+=1;stale(std::move(mode));
    auto duplicate=flow;duplicate.awaits.push_back(duplicate.awaits.front());stale(std::move(duplicate));
#endif

    auto graph=Take<ValueProgram>(ImportPhasedValues(input),"import");
    auto plan=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input),"plan");
    auto keys=Take<IteratorKeys>(ResolveIteratorKeys({*file,header,index},method->offset,graph),"keys");
    auto scan=Take<IteratorProtocolScan>(ScanIteratorProtocols(graph,input,plan,keys),"scan");
#ifdef ABCD_TEST_FOR_OF_ASYNC
    auto unowned=flow;unowned.awaits.clear();
    const PhasedValueInput missing{unowned,ssa,env,values,proof,frame};
    test::Check(std::holds_alternative<CloseEntryFailure>(DeriveCloseEntries(body,header.profile,graph,missing,plan,keys,scan)));
#endif

    auto summary=Take<CloseCompletionSummary>(AnalyzeCloseCompletion(body,header.profile,graph,input,plan,keys,scan,{},SourceContract::kLanguageSourceV4),"summary");
    const auto seal=[&](CloseCompletionSummary value,SourceContract contract=SourceContract::kLanguageSourceV4) {
      return VerifyProtocolRegionOwnership(body,header.profile,graph,input,plan,keys,scan,std::move(value),{},contract);
    };
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(summary,SourceContract::kLanguageSourceV3)));
    auto bad=summary;bad.entries[0].pending.payload=CompletionEntryPayload{999};
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    bad=summary;bad.regions[0].boundary->ports.pop_back();
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    bad=summary;bad.regions[0].boundary->regions.push_back({999});
    test::Check(std::holds_alternative<CloseCompletionFailure>(seal(std::move(bad))));
    auto sealed=seal(summary);
    if(std::holds_alternative<CloseCompletionFailure>(sealed))
      for(const auto& entry:summary.entries)for(const auto& obligation:entry.obligations)
        if(obligation.missing) {
          const auto& f=*obligation.missing;std::cerr<<"seal-gap="<<unsigned(f.kind);
          if(f.ownership_gap)std::cerr<<" ownership="<<unsigned(*f.ownership_gap);
          if(f.node)std::cerr<<" pc="<<flow.instructions[flow.nodes[f.node->value].instruction.value].pc.value;
          if(f.edge)std::cerr<<" edge-node="<<f.edge->source.value<<"/"<<unsigned(f.edge->successor);
          std::cerr<<'\n';
        }
    if(expected_gap){test::Check(std::holds_alternative<CloseCompletionFailure>(sealed));const auto& f=std::get<CloseCompletionFailure>(sealed);test::Check(f.kind==CloseCompletionError::kOwnership);std::cout<<"live-out fixture retains CloseCompletionError::kOwnership\n";return;}
    auto owner=Take<VerifiedProtocolRegionOwnership>(std::move(sealed),"seal");
    if(!owner.Forwarding().regions.empty()) {
      const auto& f=owner.Forwarding();
      std::vector<std::uint8_t> owned(plan.regions.size()),retained(plan.regions.size()),entered(plan.regions.size()),close(graph.blocks.size());
      std::vector<std::uint8_t> raw(input.flow.nodes.size());
      for(const auto& region:owner.Summary().regions){for(const auto r:region.boundary->regions)owned[r.value]=entered[r.value]=1;for(const auto r:region.boundary->enclosures){owned[r.value]=1;retained[r.value]=1;}for(const auto m:region.members)raw[m.node.value]=1;}
      for(std::size_t b=0;b<close.size();++b)close[b]=raw[graph.phases[b].input.value];
      const auto build=[&](const ValueProgram& g,const std::vector<std::uint8_t>& c,SemanticBudget b=SemanticBudget{}){return AnalyzeProtocolForwarding(g,plan,owned,c,b,retained,entered);};
      test::Check(Take<ProtocolForwarding>(build(graph,close),"forwarding")==f);
      const auto handler=std::find_if(f.blocks.begin(),f.blocks.end(),[](const auto& b){return b.has_value();})-f.blocks.begin();
      test::Check(handler<f.blocks.size());auto overlap=close;overlap[handler]=1;
      test::Check(std::holds_alternative<ForInFailure>(build(graph,overlap)));
      const auto op=std::find_if(f.operations.begin(),f.operations.end(),[](const auto& o){return o.has_value();})-f.operations.begin();test::Check(op<graph.ops.size());
      auto effectful=graph;effectful.ops[op].normal=LanguageEffect::kHeapWrite;
      test::Check(std::holds_alternative<ForInFailure>(build(effectful,close)));
      auto external=graph;bool changed=false;
      for(auto& edge:external.edges)if(edge.kind==FlowEdgeKind::kException && f.blocks[edge.target.value] && !f.blocks[edge.source.value]){edge.kind=FlowEdgeKind::kFallthrough;changed=true;break;}
      test::Check(changed && std::holds_alternative<ForInFailure>(build(external,close)));
      const auto work=f.work;Take<ProtocolForwarding>(build(graph,close,{SemanticBudget{}.bytes,work}),"forwarding-exact-work");
      test::Check(std::get<ForInFailure>(build(graph,close,{SemanticBudget{}.bytes,work-1})).kind==ForInError::kWorkLimit);
      std::size_t lo=0,hi=SemanticBudget{}.bytes;while(lo<hi){const auto mid=lo+(hi-lo)/2;auto result=build(graph,close,{mid,SemanticBudget{}.work});if(std::holds_alternative<ProtocolForwarding>(result))hi=mid;else{test::Check(std::get<ForInFailure>(result).kind==ForInError::kResourceLimit);lo=mid+1;}}
      Take<ProtocolForwarding>(build(graph,close,{lo,SemanticBudget{}.work}),"forwarding-exact-bytes");test::Check(lo && std::get<ForInFailure>(build(graph,close,{lo-1,SemanticBudget{}.work})).kind==ForInError::kResourceLimit);
    }
    auto built_control=BuildExceptionControl(body,graph,input,plan,{},&owner);
    auto control=Take<ExceptionControl>(std::move(built_control),"control");
    const auto verify=[&](const ForOfIdentityIndex& value) {
      return VerifyForOfIdentityIndex(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,value);
    };
    auto protocol_result=IndexForOfIdentities(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4);
    if(const auto* f=std::get_if<ForOfIdentityFailure>(&protocol_result)) {
      std::cerr<<"forof kind="<<unsigned(f->kind);
      if(f->operation)std::cerr<<" op="<<f->operation->value<<" pc="<<graph.origins[graph.ops[f->operation->value].origin.value].pc.value;
      if(f->use)std::cerr<<" use="<<f->use->value;
      std::cerr<<'\n';
    }
    auto protocol=Take<ForOfIdentityIndex>(std::move(protocol_result),"forof");
    Take<std::uint64_t>(verify(protocol),"verify-forof");
    auto advancing=ProveForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol);
    if(const auto* f=std::get_if<ForOfIterationFailure>(&advancing)){std::cerr<<"iteration kind="<<unsigned(f->kind);if(f->node)std::cerr<<" node="<<f->node->value;if(f->operation)std::cerr<<" op="<<f->operation->value;if(f->edge)std::cerr<<" edge="<<f->edge->value;std::cerr<<'\n';}
    auto iteration=Take<ForOfIterationProof>(std::move(advancing),"iteration");
#ifdef ABCD_TEST_FOR_OF_EVALUATION
    const ForOfEvaluationInput source{body,input,plan,control,owner,SourceContract::kLanguageSourceV4};
    auto evaluation_protocol=Take<ForOfEvaluationPlan>(BuildForOfEvaluationPlan(graph,source),"evaluation-protocol");
#ifdef ABCD_TEST_FOR_OF_CELLS
    {
    auto cell_environments=Take<ValueEnvironments>(BuildPhasedValueEnvironments(graph),"cell-environments");
    auto cell_plan=Take<CellPlan>(BuildPhasedCellPlan(graph,cell_environments),"cell-plan");
    auto view=Take<ForOfEmissionView>(BuildForOfEmissionView(graph,source,evaluation_protocol),"cell-emission-view");
    const ExceptionTreeInput cell_input{body,input,plan,control,source};
    const auto lifetimes = [&](const ExceptionTreeInput& context,
        const CellPlan& cells, const ForOfEmissionView& projection,
        SemanticBudget budget = SemanticBudget{}) {
      return BuildPhasedCellLifetimes(graph,cell_environments,cells,context,budget,
                                     &evaluation_protocol,&projection);
    };
    const auto scopes=Take<CellLifetimes>(lifetimes(cell_input,cell_plan,view),"cell-lifetimes");
    test::Check(!cell_plan.cells.empty());
    bool loop_access=false;
    for(const auto& access:cell_plan.accesses) {
      const auto block=graph.ops[access.operation.value].block;
      test::Check(scopes.blocks[block.value].has_value());
      for(auto scope=scopes.blocks[block.value];scope;scope=scopes.scopes[scope->value].parent)
        if(const auto* loop=std::get_if<CellLoopScope>(&scopes.scopes[scope->value].origin)) {
          test::Check(loop->loop==view.loops[0].loop);loop_access=true;
        }
    }
    test::Check(loop_access);
    for(std::size_t b=0;b<view.close_blocks.size();++b)
      if(view.close_blocks[b])test::Check(!scopes.blocks[b]);
    auto no_seal=cell_input;no_seal.for_of.reset();
    test::Check(std::get<CellLifetimeError>(lifetimes(no_seal,cell_plan,view))==CellLifetimeError::kControl);
    auto wrong_view=view;wrong_view.ports[0].pending.target=ExceptionNodeId{UINT32_MAX};
    test::Check(std::get<CellLifetimeError>(lifetimes(cell_input,cell_plan,wrong_view))==CellLifetimeError::kControl);
    wrong_view=view;wrong_view.ports.pop_back();
    test::Check(std::get<CellLifetimeError>(lifetimes(cell_input,cell_plan,wrong_view))==CellLifetimeError::kControl);
    wrong_view=view;wrong_view.loops[0].loop={UINT32_MAX};
    test::Check(std::get<CellLifetimeError>(lifetimes(cell_input,cell_plan,wrong_view))==CellLifetimeError::kControl);
    auto wrong_cells=cell_plan;
    bool declaration=false;
    for(auto& cell:wrong_cells.cells)if(auto* at=std::get_if<AllocatedCell>(&cell.storage)) {
      at->allocation=protocol.loops[0].acquisition;declaration=true;break;
    }
    test::Check(declaration && std::get<CellLifetimeError>(lifetimes(cell_input,wrong_cells,view))==CellLifetimeError::kCells);
    test::Check(std::get<CellLifetimeError>(lifetimes(cell_input,cell_plan,view,
        {SemanticBudget{}.bytes,scopes.work-1}))==CellLifetimeError::kWorkLimit);
    Take<CellLifetimes>(lifetimes(cell_input,cell_plan,view,
        {SemanticBudget{}.bytes,scopes.work}),"cell-exact-work");
    std::size_t low=0,high=SemanticBudget{}.bytes;
    while(low<high){const auto mid=low+(high-low)/2;
      auto test=lifetimes(cell_input,cell_plan,view,{mid,SemanticBudget{}.work});
      if(std::holds_alternative<CellLifetimes>(test))high=mid;
      else {test::Check(std::get<CellLifetimeError>(test)==CellLifetimeError::kResourceLimit);low=mid+1;}
    }
    Take<CellLifetimes>(lifetimes(cell_input,cell_plan,view,{low,scopes.work}),"cell-exact-both");
    test::Check(low && std::get<CellLifetimeError>(lifetimes(cell_input,cell_plan,view,
        {low-1,scopes.work}))==CellLifetimeError::kResourceLimit);
    std::cout<<"cell-boundary padding="<<padding<<" bytes="<<low<<" work="<<scopes.work
             <<" scopes="<<scopes.scopes.size()<<" cells="<<cell_plan.cells.size()<<'\n';
    return;
    }
#endif

    Take<std::uint64_t>(VerifyForOfEvaluationPlan(graph,source,evaluation_protocol),"verify-evaluation-protocol");
    auto forged_disposition=evaluation_protocol;forged_disposition.operations[protocol.loops[0].acquisition.value].reset();
    test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEvaluationPlan(graph,source,forged_disposition)));
    forged_disposition=evaluation_protocol;forged_disposition.values[protocol.loops[0].iterator.value].reset();
    test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEvaluationPlan(graph,source,forged_disposition)));
    const auto protocol_build=[&](SemanticBudget b){return BuildForOfEvaluationPlan(graph,source,b);};
    Take<ForOfEvaluationPlan>(protocol_build({SemanticBudget{}.bytes,evaluation_protocol.work}),"protocol-exact-work");
    test::Check(std::get<ForOfIterationFailure>(protocol_build({SemanticBudget{}.bytes,evaluation_protocol.work-1})).kind==ForOfIterationError::kWorkLimit);
    std::size_t low=0,high=SemanticBudget{}.bytes;
    while(low<high){const auto mid=low+(high-low)/2;auto e=protocol_build({mid,SemanticBudget{}.work});if(std::holds_alternative<ForOfEvaluationPlan>(e))high=mid;else{test::Check(std::get<ForOfIterationFailure>(e).kind==ForOfIterationError::kResourceLimit);low=mid+1;}}
    Take<ForOfEvaluationPlan>(protocol_build({low,SemanticBudget{}.work}),"protocol-exact-bytes");
    test::Check(low>0 && std::get<ForOfIterationFailure>(protocol_build({low-1,SemanticBudget{}.work})).kind==ForOfIterationError::kResourceLimit);
    bool body_use=false;
    for(const auto& region:owner.Summary().regions)for(const auto& range:region.boundary->protected_ranges)if(range.role==ProtectedRangeRole::kLoopBody)
      for(std::uint32_t u=0;u<graph.uses.size() && !body_use;++u){const auto& use=graph.uses[u];const auto* o=std::get_if<OpId>(&use.owner);if(use.life!=ValueLife::kLive || !o || evaluation_protocol.uses[u].protocol)continue;
        if(std::find(range.ordinary.begin(),range.ordinary.end(),graph.phases[graph.ops[o->value].block.value].input)==range.ordinary.end())continue;
        forged_disposition=evaluation_protocol;forged_disposition.uses[u].protocol=protocol.loops[0].protocol;
        test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEvaluationPlan(graph,source,forged_disposition)));body_use=true;
      }
    test::Check(body_use);
    if(!protocol_only) {
    auto full=BuildPhasedEvaluationPlan(graph,{},IncomingCells::kConditional,{},{},{},{},ClosureWrites::kLanguage,{},{},&source);
    if(const auto* f=std::get_if<EvaluationFailure>(&full)){std::cerr<<"evaluation kind="<<unsigned(f->kind)<<" obligation="<<unsigned(f->evaluation.obligation);if(f->evaluation.location.operation)std::cerr<<" op="<<f->evaluation.location.operation->value;if(f->evaluation.location.pc)std::cerr<<" pc="<<f->evaluation.location.pc->value;std::cerr<<'\n';}
    auto evaluation=Take<EvaluationPlan>(std::move(full),"evaluation");
#ifdef ABCD_TEST_ENUMERATION_COMPOSITION
    CheckEnumerationComposition(graph,source,evaluation);
#endif
    Take<std::uint64_t>(VerifyPhasedEvaluationPlan(graph,evaluation,{},&source),"verify-evaluation");
    if(evaluation.phis && evaluation.exceptions &&
        evaluation.exceptions->projection==ExceptionCopyProjection::kForOfClose) {
      for(std::size_t c=0;c<evaluation.phis->copies.size();++c) {
        if(evaluation.phis->projection[c]!=PhiCopyUse::kForOfProtocol)continue;
        test::Check(!evaluation.exceptions->staging[c]);
        auto forged=*evaluation.exceptions;
        forged.staging[c]=CatchSlotId{UINT32_MAX};
        test::Check(std::holds_alternative<ExceptionCopyError>(VerifyExceptionCopies(
            graph,*evaluation.phis,forged,{},evaluation_protocol.uses,ExceptionCopyProjection::kForOfClose)));
      }
      for(const auto& slot:evaluation.exceptions->slots)
        test::Check(!evaluation_protocol.values[slot.destination.value]);
    }
#ifdef ABCD_TEST_FOR_OF_SELECTION
    TreeCheckBudget selection_budget({});
    const auto selections=FindSelectionExpressions(graph,control.skeleton,evaluation,selection_budget,&control);
    std::optional<OpId> remapped_selection;
    for(const auto& selection:selections) {
      for(std::size_t n=0;n<control.units.size();++n) {
        const auto* phase=std::get_if<PhaseControlUnit>(&control.units[n]);
        if(!phase || phase->block!=selection.header || control.skeleton.nodes[n].block==phase->block)continue;
        test::Check(!evaluation.for_of->operations[selection.branch.value]);
        remapped_selection=selection.branch;
        auto wrong=control;std::get<PhaseControlUnit>(wrong.units[n]).block=selection.join;
        test::Check(std::holds_alternative<ExceptionControlError>(VerifyExceptionControl(body,graph,input,plan,wrong,{},&owner)));
        TreeCheckBudget mutated_budget({});
        const auto missing=FindSelectionExpressions(graph,wrong.skeleton,evaluation,mutated_budget,&wrong);
        test::Check(std::none_of(missing.begin(),missing.end(),[&](const auto& r){return r.branch==selection.branch;}));
        break;
      }
    }
    test::Check(remapped_selection.has_value());
#endif
    for(std::uint32_t v=0;v<graph.values.size();++v)if(evaluation.for_of->values[v])if(const auto* op=std::get_if<OpResult>(&graph.values[v].origin))test::Check(evaluation.operations[op->op.value].kind==EvaluationKind::kForOfEvent && !evaluation.operations[op->op.value].temporary);
    if(evaluation.phis)for(std::size_t n=0;n<evaluation.phis->inputs.size();++n){const auto& use=graph.uses[evaluation.phis->inputs[n].value];const auto target=std::get<ValueId>(use.owner);if(evaluation.for_of->values[target.value])test::Check(evaluation.phis->projection[n]==PhiCopyUse::kForOfProtocol);}
    const auto full_build=[&](SemanticBudget b){return BuildPhasedEvaluationPlan(graph,b,IncomingCells::kConditional,{},{},{},{},ClosureWrites::kLanguage,{},{},&source);};
    Take<EvaluationPlan>(full_build({SemanticBudget{}.bytes,evaluation.work}),"evaluation-exact-work");
    test::Check(std::get<EvaluationFailure>(full_build({SemanticBudget{}.bytes,evaluation.work-1})).kind==EvaluationError::kWorkLimit);

    auto leak=evaluation;leak.operations[protocol.loops[0].advance.value]={EvaluationKind::kRoot,{},TemporaryReason::kCrossRegionValue};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph,leak,{},&source)));
    const auto missing_context = VerifyPhasedEvaluationPlan(graph,evaluation);
    test::Check(std::holds_alternative<EvaluationFailure>(missing_context));
    const auto& context_failure = std::get<EvaluationFailure>(missing_context).evaluation;
    test::Check(context_failure.obligation == EvaluationObligation::kProtocolContext &&
        context_failure.protocol_context == ProtocolContextCheck::kMissingInput &&
        context_failure.location.operation == protocol.loops[0].acquisition);
    const auto location_work = std::uint64_t(protocol.loops[0].acquisition.value) + 1;
    const auto located = VerifyForOfEvaluation(graph,nullptr,evaluation,{0,location_work});
    test::Check(std::get<EvaluationFailure>(located).evaluation == context_failure);
    test::Check(std::get<EvaluationFailure>(VerifyForOfEvaluation(graph,nullptr,evaluation,{0,location_work-1})).kind == EvaluationError::kWorkLimit);
    auto wrong_binding=evaluation;
    const auto bound=std::find_if(wrong_binding.for_of->bindings.begin(),wrong_binding.for_of->bindings.end(),[](const auto& b){return b.has_value();});
    test::Check(bound!=wrong_binding.for_of->bindings.end());bound->reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph,wrong_binding,{},&source)));
    auto missing_plan = evaluation; missing_plan.for_of.reset();
    const auto absent = VerifyPhasedEvaluationPlan(graph,missing_plan,{},&source);
    test::Check(std::get<EvaluationFailure>(absent).evaluation.protocol_context == ProtocolContextCheck::kMissingPlan);
#ifdef ABCD_TEST_ITERATION_CONTEXT
    test::Check(evaluation.for_in && evaluation.for_of);
#endif
#ifdef ABCD_TEST_FOR_OF_DRAFT
    auto view=Take<ForOfEmissionView>(BuildForOfEmissionView(graph,source,*evaluation.for_of),"emission-view");
    Take<std::uint64_t>(VerifyForOfEmissionView(graph,source,*evaluation.for_of,view),"verify-emission-view");
    auto forged_view=view;forged_view.loops[0].success=forged_view.loops[0].exhausted;
    test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEmissionView(graph,source,*evaluation.for_of,forged_view)));
    forged_view=view;test::Check(!forged_view.ports.empty());forged_view.ports[0].outcome.pending.target=ExceptionNodeId{UINT32_MAX};
    test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEmissionView(graph,source,*evaluation.for_of,forged_view)));
    forged_view=view;test::Check(!forged_view.ports.empty());forged_view.ports.pop_back();
    test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEmissionView(graph,source,*evaluation.for_of,forged_view)));
    if(!view.forwarding.regions.empty()) {
      forged_view=view;const auto r=std::find_if(forged_view.forwarding.regions.begin(),forged_view.forwarding.regions.end(),[](const auto& f){return f.has_value();})-forged_view.forwarding.regions.begin();
      forged_view.forwarding.regions[r]->payload={UINT32_MAX};
      test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEmissionView(graph,source,*evaluation.for_of,forged_view)));
      forged_view=view;forged_view.forwarding.exits.clear();
      test::Check(std::holds_alternative<ForOfIterationFailure>(VerifyForOfEmissionView(graph,source,*evaluation.for_of,forged_view)));
    }
    auto missing_exception=evaluation;
    test::Check(!missing_exception.for_of->events.events.empty() && missing_exception.for_of->events.events[0].exception_exit);
    missing_exception.for_of->events.events[0].exception_exit.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph,missing_exception,{},&source)));
    auto constants=Take<TreeConstants>(ResolveTreeConstants(*file,header,index,method->offset,graph),"constants");
    FunctionSyntax syntax{};syntax.owner={0};syntax.method=method->offset;syntax.identity.contract=SourceContract::kLanguageSourceV4;
    syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
    syntax.parameters.resize(frame.parameters);syntax.names=constants.names;syntax.texts=constants.texts;
    const auto resolved=constants.View();const ExceptionTreeInput exceptions{body,input,plan,control,source};
    const auto build_draft=[&](SemanticBudget b=SemanticBudget{}){return BuildForOfTreeDraft(graph,exceptions,evaluation,syntax,resolved,b);};
    auto drafted=build_draft();
    if(const auto* f=std::get_if<FunctionTreeFailure>(&drafted)){std::cerr<<"draft kind="<<unsigned(f->kind);if(const auto* e=std::get_if<ExpressionFailure>(&f->cause))std::cerr<<" expression="<<unsigned(e->kind);if(f->tree){std::cerr<<" obligation="<<unsigned(f->tree->obligation);if(f->tree->location.operation)std::cerr<<" op="<<f->tree->location.operation->value;if(f->tree->location.pc)std::cerr<<" pc="<<f->tree->location.pc->value;}std::visit([](const auto& c){if constexpr(std::is_enum_v<std::decay_t<decltype(c)>>)std::cerr<<" cause="<<unsigned(c);},f->cause);std::cerr<<'\n';}
    auto draft=Take<ForOfTreeDraft>(std::move(drafted),"draft");
    test::Check(draft.flow.for_of && !draft.flow.original);
    const auto work=draft.work;
    Take<ForOfTreeDraft>(build_draft({SemanticBudget{}.bytes,work}),"draft-exact-work");
    test::Check(std::get<FunctionTreeFailure>(build_draft({SemanticBudget{}.bytes,work-1})).kind==FunctionTreeError::kWorkLimit);
    std::size_t byte_low=0,byte_high=SemanticBudget{}.bytes;
    while(byte_low<byte_high){const auto mid=byte_low+(byte_high-byte_low)/2;auto candidate=build_draft({mid,SemanticBudget{}.work});
      if(std::holds_alternative<ForOfTreeDraft>(candidate))byte_high=mid;
      else {test::Check(std::get<FunctionTreeFailure>(candidate).kind==FunctionTreeError::kResourceLimit);byte_low=mid+1;}}
    Take<ForOfTreeDraft>(build_draft({byte_low,SemanticBudget{}.work}),"draft-exact-bytes");
    test::Check(byte_low>0 && std::get<FunctionTreeFailure>(build_draft({byte_low-1,SemanticBudget{}.work})).kind==FunctionTreeError::kResourceLimit);
    std::cout<<"draft-boundary bytes="<<byte_low<<" work="<<work<<'\n';

    if(!draft_output.empty()){std::ofstream output(draft_output);output<<"export default ";auto printed=PrintFunctionTree(draft.tree,output);test::Check(std::holds_alternative<PrintedFunction>(printed));output<<";\n";}
    std::cout<<"verified production for-of draft statements="<<draft.tree.statements.size()<<'\n';
    TreeLocation location;
    Take<TreeFlow>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,draft.tree,{},TreeControlCheck::kFresh,&location),"independent-tree-flow");
    auto rhs_tree=draft.tree;
    for(const auto& st:rhs_tree.statements)if(const auto* loop=std::get_if<ForOfStmt>(&st.statement)){rhs_tree.expressions[loop->iterable.value].expression=SymbolExpr{loop->target};break;}
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,rhs_tree)));
    auto bad_tree=draft.tree;bool changed=false;
    for(auto& st:bad_tree.statements)if(auto* loop=std::get_if<ForOfStmt>(&st.statement)){loop->target={static_cast<std::uint32_t>(bad_tree.symbols.size())};changed=true;break;}
    test::Check(changed && std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,bad_tree)));
    bad_tree=draft.tree;
    const auto native=*std::find_if(bad_tree.statements.begin(),bad_tree.statements.end(),[](const auto& s){return std::holds_alternative<ForOfStmt>(s.statement);});
    const auto target=std::get<ForOfStmt>(native.statement).target;
    const ExprId undefined{static_cast<std::uint32_t>(bad_tree.expressions.size())};bad_tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}},native.origin});
    const ExprId put{static_cast<std::uint32_t>(bad_tree.expressions.size())};bad_tree.expressions.push_back({AssignmentExpr{SymbolTarget{target},undefined},native.origin});
    const StmtId write{static_cast<std::uint32_t>(bad_tree.statements.size())};bad_tree.statements.push_back({ExpressionStmt{put},native.origin});
    auto& root=std::get<BlockStmt>(bad_tree.statements[bad_tree.body.value].statement);
    std::vector<StmtId> original_children(bad_tree.children.begin()+root.first_child,bad_tree.children.begin()+root.first_child+root.child_count);
    root.first_child=static_cast<std::uint32_t>(bad_tree.children.size());root.child_count++;
    for(const auto child:original_children){bad_tree.children.push_back(child);if(std::holds_alternative<ForOfStmt>(bad_tree.statements[child.value].statement))bad_tree.children.push_back(write);}
    // Repack all block slices, so this mutation cannot fail merely because an
    // abandoned child slice exists. It fails the execution correspondence.
    std::vector<StmtId> packed;
    for(auto& st:bad_tree.statements)if(auto* block=std::get_if<BlockStmt>(&st.statement)){const auto first=static_cast<std::uint32_t>(packed.size());packed.insert(packed.end(),bad_tree.children.begin()+block->first_child,bad_tree.children.begin()+block->first_child+block->child_count);block->first_child=first;}
    bad_tree.children=std::move(packed);
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,bad_tree)));
    bad_tree=draft.tree;bool missing_continue=false;
    const auto native_body=std::get<ForOfStmt>(native.statement).body;
    (void)native_body;
    for(std::uint32_t s=0;s<bad_tree.statements.size();++s)if(std::holds_alternative<ContinueStmt>(bad_tree.statements[s].statement)) {
      std::optional<ScopeId> parent_scope;
      for(const auto& statement:bad_tree.statements)if(const auto* block=std::get_if<BlockStmt>(&statement.statement))
        for(std::uint32_t c=0;c<block->child_count;++c)if(bad_tree.children[block->first_child+c]==StmtId{s}) {
          test::Check(!parent_scope);parent_scope=block->scope;
        }
      test::Check(parent_scope.has_value());
      const ScopeId scope{static_cast<std::uint32_t>(bad_tree.scopes.size())};bad_tree.scopes.push_back({ScopeKind::kBlock,*parent_scope});
      bad_tree.statements[s].statement=BlockStmt{scope,0,0,{}};missing_continue=true;break;
    }
    test::Check(missing_continue && std::holds_alternative<TreeStructure>(VerifyTreeStructure(bad_tree)));
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,bad_tree)));
#ifdef ABCD_TEST_FOR_OF_SELECTION
    const auto selected=std::find_if(draft.tree.operations.begin(),draft.tree.operations.end(),[&](const auto& p){return p.operation==*remapped_selection;});
    test::Check(selected!=draft.tree.operations.end());
    auto dropped=draft.tree;dropped.operations.erase(dropped.operations.begin()+(selected-draft.tree.operations.begin()));
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,dropped)));
    auto doubled=draft.tree;doubled.operations.push_back(*selected);
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph,exceptions,evaluation,doubled)));
    auto swapped=draft.tree;bool swapped_arms=false;
    for(auto& expression:swapped.expressions)if(auto* conditional=std::get_if<ConditionalExpr>(&expression.expression)) {
      std::swap(conditional->consequent,conditional->alternate);swapped_arms=true;break;
    }
    test::Check(swapped_arms && !std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph,exceptions,evaluation,swapped,resolved)));
#endif
    auto semantic=VerifyExceptionTreeSemantics(graph,exceptions,evaluation,draft.tree,resolved);
    if(const auto* error=std::get_if<TreeSemanticError>(&semantic))std::cerr<<"for-of semantics error="<<unsigned(*error)<<'\n';
    auto sem=Take<TreeSemantics>(std::move(semantic),"for-of-original-flow");
#ifdef ABCD_TEST_LOCAL_TRANSFER
    {
      auto changed=draft.tree;bool retargeted=false;
      for(auto& node:changed.statements)if(auto* jump=std::get_if<BreakStmt>(&node.statement)) {
        if(!std::holds_alternative<LoopStmt>(changed.statements[jump->target.value].statement))continue;
        jump->target=sem.flow.original->iterations->statements.front();retargeted=true;break;
      }
      test::Check(retargeted);
      Take<TreeStorage>(AssignTreeLabels(changed),"retarget-labels");
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph,exceptions,evaluation,changed,resolved)));
    }
#endif
    auto recovery=VerifyInitialForOfRecovery(draft.tree,*sem.flow.original);
    if(const auto* error=std::get_if<TreeStructureError>(&recovery))std::cerr<<"for-of recovery error="<<unsigned(*error)<<'\n';
    const auto recovery_storage=Take<TreeStorage>(std::move(recovery),"for-of-recovery");
    std::vector<std::optional<SymbolId>> aliases(draft.tree.symbols.size());
    std::vector<std::optional<PrimitiveId>> literals(draft.tree.symbols.size());
    std::vector<std::optional<ExprId>> replacements(draft.tree.expressions.size());
    std::vector<std::uint8_t> omitted(draft.tree.statements.size()),normal(draft.tree.expressions.size());
    std::vector<ExpressionEvaluation> effects(draft.tree.expressions.size(),ExpressionEvaluation::kRequired);
    for(std::uint32_t i=0;i<aliases.size();++i)aliases[i]=SymbolId{i};
    const auto recover=[&](const OriginalFlow& raw,SemanticBudget budget=SemanticBudget{}) {
      LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&raw;
      return BuildRecoveryFlow(draft.tree,projection,budget);
    };
    auto recovered=Take<RecoveryFlow>(recover(*sem.flow.original),"joint-recovery");
    const auto statement=sem.flow.original->iterations->statements[0];
    auto wrong_alias=*sem.flow.original;
    wrong_alias.iterations->binding_values[protocol.loops[0].key.value].reset();
    test::Check(std::holds_alternative<TreeStructureError>(recover(wrong_alias)));
    auto bad_flow=recovered;
    for(auto& edge:bad_flow.edges)if(edge.from==bad_flow.iterations[statement.value]->done && edge.completion==FlowCompletion::kTaken){edge.to=bad_flow.iterations[statement.value]->write;break;}
    const auto reject_flow=[&](RecoveryFlow& proposal){try{TreeCheckBudget budget({});VerifyForOfRecovery(draft.tree,proposal,budget);test::Check(false);}catch(TreeStructureError error){test::Check(error==TreeStructureError::kOwnership);}};
    reject_flow(bad_flow);
    bad_flow=recovered;const auto close_index=bad_flow.iterations[statement.value]->throw_close;
    bad_flow.iteration_closes[close_index].outcomes[static_cast<std::size_t>(CloseOutcomeKind::kGetterThrow)-1].payload=FlowCloseErrorPayload{bad_flow.iteration_closes[close_index].read};
    reject_flow(bad_flow);
    if(recovered.iterations[statement.value]->thrown!=recovered.terminal) {
      bad_flow=recovered;
      const auto index=bad_flow.iterations[statement.value]->throw_close;
      bad_flow.iteration_closes[index].thrown=bad_flow.terminal;
      reject_flow(bad_flow); // Enclosing catch may not be replaced by caller exit.
    }
    bad_flow=recovered;bool wrong_continue=false;
    for(const auto& exit:sem.flow.original->iterations->exits)if(exit.kind==LoopTransferKind::kContinue){for(auto& edge:bad_flow.edges)if(edge.from==bad_flow.statements[exit.statement.value]&&edge.completion==FlowCompletion::kJump){edge.to=bad_flow.iterations[statement.value]->exit;wrong_continue=true;break;}break;}
    test::Check(wrong_continue);reject_flow(bad_flow);
    if(!sem.flow.original->iterations->view.forwarding.regions.empty()) {
      auto raw=*sem.flow.original;auto& f=raw.iterations->view.forwarding;
      const auto r=std::find_if(f.regions.begin(),f.regions.end(),[](const auto& x){return x.has_value();})-f.regions.begin();
      f.regions[r]->payload=raw.iterations->loops[0].iterator;
      test::Check(std::holds_alternative<TreeStructureError>(recover(raw)));
      raw=*sem.flow.original;auto& entries=raw.iterations->view.forwarding.exits;
      const auto e=std::find_if(entries.begin(),entries.end(),[](const auto& x){return x.has_value();})-entries.begin();test::Check(e<entries.size());entries[e].reset();
      test::Check(std::holds_alternative<TreeStructureError>(recover(raw)));
    }
    auto wrong_raw=*sem.flow.original;wrong_raw.iterations->events.ports[0].outcome.pending.target=ExceptionNodeId{UINT32_MAX};
    test::Check(std::holds_alternative<TreeStructureError>(recover(wrong_raw)));
    wrong_raw=*sem.flow.original;wrong_raw.iterations->loops[0].advance=wrong_raw.iterations->loops[0].done;
    test::Check(std::holds_alternative<TreeStructureError>(recover(wrong_raw)));
    wrong_raw=*sem.flow.original;
    const auto& call=wrong_raw.operations[wrong_raw.iterations->loops[0].advance.value];
    for(std::uint32_t u=call.first_use.value;u<call.first_use.value+call.use_count;++u)if(wrong_raw.uses[u].role==OperandRole::kReceiver)wrong_raw.uses[u].operand=wrong_raw.iterations->loops[0].cached_next;
    test::Check(std::holds_alternative<TreeStructureError>(recover(wrong_raw)));
    for(std::size_t p=0;p<sem.flow.original->iterations->events.ports.size();++p)
      if(sem.flow.original->iterations->events.ports[p].pending.kind==PendingCompletionKind::kReturn) {
        wrong_raw=*sem.flow.original;
        wrong_raw.iterations->events.ports[p].pending.payload=CompletionEntryPayload{UINT32_MAX};
        test::Check(std::holds_alternative<TreeStructureError>(recover(wrong_raw)));break;
      }
    wrong_raw=*sem.flow.original;wrong_raw.iterations->events.events[0].exception_exit.reset();
    test::Check(std::holds_alternative<TreeStructureError>(recover(wrong_raw)));
    const auto exact=Take<TreeStorage>(VerifyInitialForOfRecovery(draft.tree,*sem.flow.original,{SemanticBudget{}.bytes,recovery_storage.work}),"recovery-exact-work");
    test::Check(std::get<TreeStructureError>(VerifyInitialForOfRecovery(draft.tree,*sem.flow.original,{SemanticBudget{}.bytes,recovery_storage.work-1}))==TreeStructureError::kWorkLimit);
    std::size_t low=0,high=SemanticBudget{}.bytes;
    while(low<high){const auto mid=low+(high-low)/2;const auto probe=VerifyInitialForOfRecovery(draft.tree,*sem.flow.original,{mid,SemanticBudget{}.work});if(std::holds_alternative<TreeStorage>(probe))high=mid;else {test::Check(std::get<TreeStructureError>(probe)==TreeStructureError::kResourceLimit);low=mid+1;}}
    Take<TreeStorage>(VerifyInitialForOfRecovery(draft.tree,*sem.flow.original,{low,SemanticBudget{}.work}),"recovery-exact-bytes");
    test::Check(low>0 && std::get<TreeStructureError>(VerifyInitialForOfRecovery(draft.tree,*sem.flow.original,{low-1,SemanticBudget{}.work}))==TreeStructureError::kResourceLimit);
    std::cout<<"recovery-boundary bytes="<<low<<" work="<<exact.work<<'\n';
    const auto gated=BuildExceptionFunctionDraft(graph,exceptions,evaluation,syntax,resolved);
    if(const auto* error=std::get_if<FunctionTreeFailure>(&gated))std::cerr<<"published builder error="<<unsigned(error->kind)<<'\n';
    test::Check(!std::holds_alternative<FunctionTreeFailure>(gated));
#endif
    std::cout<<"verified common evaluation plan\n";
    }
    std::cout<<"verified for-of disposition\n";
#endif
    const auto verify_iteration=[&](const ForOfIterationProof& value){return VerifyForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,value);};
    Take<std::uint64_t>(verify_iteration(iteration),"verify-iteration");
    test::Check(iteration.references.size()==protocol.loops.size()*3);
    auto bad_reference=iteration;bad_reference.references[0].root=protocol.loops[0].advance;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(bad_reference)));
    bad_reference=iteration;std::swap(bad_reference.references[1].use,bad_reference.references[2].use);
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(bad_reference)));
    auto events=Take<ForOfEvents>(BuildForOfEvents(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration),"events");
    const auto verify_events=[&](const ForOfEvents& e){return VerifyForOfEvents(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration,e);};
    Take<std::uint64_t>(verify_events(events),"verify-events");
    auto wrong_event=events;wrong_event.events[0].commit=wrong_event.events[0].before;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    wrong_event=events;wrong_event.events[2].exception_exit.reset();
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    wrong_event=events;wrong_event.commits[0].success=wrong_event.commits[0].exhausted;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    wrong_event=events;wrong_event.ports[0].pending.target=ExceptionNodeId{0};
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_events(wrong_event)));
    const auto event_build=[&](SemanticBudget b){return BuildForOfEvents(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration,b);};
    Take<ForOfEvents>(event_build({SemanticBudget{}.bytes,events.work}),"events-exact-work");
    test::Check(std::get<ForOfIterationFailure>(event_build({SemanticBudget{}.bytes,events.work-1})).kind==ForOfIterationError::kWorkLimit);
    std::size_t elow=0,ehigh=SemanticBudget{}.bytes;
    while(elow<ehigh){const auto mid=elow+(ehigh-elow)/2;auto e=event_build({mid,SemanticBudget{}.work});if(std::holds_alternative<ForOfEvents>(e))ehigh=mid;else{test::Check(std::get<ForOfIterationFailure>(e).kind==ForOfIterationError::kResourceLimit);elow=mid+1;}}
    Take<ForOfEvents>(event_build({elow,SemanticBudget{}.work}),"events-exact-bytes");
    test::Check(elow>0 && std::get<ForOfIterationFailure>(event_build({elow-1,SemanticBudget{}.work})).kind==ForOfIterationError::kResourceLimit);
    std::cout<<"references="<<iteration.references.size()<<" events="<<events.events.size()<<" commits="<<events.commits.size()<<" ports="<<events.ports.size()<<'\n';
    for(const auto contract:{kSourceContract,SourceContract::kLanguageSourceV1})test::Check(std::get<ForOfIterationFailure>(ProveForOfIterations(body,graph,input,plan,control,owner,contract,protocol)).kind==ForOfIterationError::kAcquisitionSemantics);
    auto altered=iteration;altered.iterations[0].advance=altered.iterations[0].decision;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;std::swap(altered.iterations[0].success,altered.iterations[0].exhausted);
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    test::Check(!iteration.definitions.empty());altered=iteration;altered.definitions[0].original.value^=1;
    test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;altered.definitions.pop_back();test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    const auto advancing_build=[&](SemanticBudget b){return ProveForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,b);};
    Take<ForOfIterationProof>(advancing_build({SemanticBudget{}.bytes,iteration.work}),"iteration-exact-work");
    test::Check(std::get<ForOfIterationFailure>(advancing_build({SemanticBudget{}.bytes,iteration.work-1})).kind==ForOfIterationError::kWorkLimit);
    altered=iteration;altered.definitions.push_back(altered.definitions.front());test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;altered.definitions[0].operation=protocol.loops[0].decision;test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    altered=iteration;altered.iterations[0].value=altered.iterations[0].header;test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));
    auto forged_control=control;test::Check(!forged_control.protocol_ports.empty());forged_control.protocol_ports[0].pending.target=ExceptionNodeId{0};
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,graph,input,plan,forged_control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto deleted_use=graph;test::Check(!deleted_use.uses.empty());deleted_use.uses.front().life=ValueLife::kRemoved;
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,deleted_use,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto swapped_reference=graph;
    const auto& next_call=graph.ops[protocol.loops[0].advance.value];
    std::optional<UseId> callee_use,receiver_use;
    for(std::uint32_t u=next_call.first_use.value;u<next_call.first_use.value+next_call.use_count;++u){if(graph.uses[u].role==OperandRole::kCallee)callee_use=UseId{u};if(graph.uses[u].role==OperandRole::kReceiver)receiver_use=UseId{u};}
    test::Check(callee_use && receiver_use);
    const auto callee_value=std::get<ValueId>(graph.uses[callee_use->value].operand),receiver_value=std::get<ValueId>(graph.uses[receiver_use->value].operand);
    test::Check(!ReplaceUse(swapped_reference,*callee_use,receiver_value));
    test::Check(!ReplaceUse(swapped_reference,*receiver_use,callee_value));
    test::Check(!VerifyValueUses(swapped_reference,swapped_reference.uses.size()));
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,swapped_reference,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto escape=graph;std::optional<UseId> external_use;
    for(std::uint32_t u=0;u<graph.uses.size();++u)if(graph.uses[u].life==ValueLife::kLive && !protocol.uses[u] && std::holds_alternative<ValueId>(graph.uses[u].operand) && std::holds_alternative<OpId>(graph.uses[u].owner)){external_use=UseId{u};break;}
    test::Check(external_use && graph.ops[protocol.loops[0].acquisition.value].result);
    test::Check(!ReplaceUse(escape,*external_use,*graph.ops[protocol.loops[0].acquisition.value].result));
    test::Check(!VerifyValueUses(escape,escape.uses.size()));
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,escape,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol)));
    auto missing_exit=graph;const auto call_exit=std::find_if(missing_exit.exceptional_exits.begin(),missing_exit.exceptional_exits.end(),[&](const auto& e){return e.op==protocol.loops[0].advance;});test::Check(call_exit!=missing_exit.exceptional_exits.end());missing_exit.exceptional_exits.erase(call_exit);
    test::Check(std::holds_alternative<ForOfIterationFailure>(BuildForOfEvents(body,missing_exit,input,plan,control,owner,SourceContract::kLanguageSourceV4,protocol,iteration)));
    auto wrong_identity=protocol;wrong_identity.loops[0].next=wrong_identity.loops[0].advance;
    test::Check(std::holds_alternative<ForOfIterationFailure>(ProveForOfIterations(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,wrong_identity)));
    if(!iteration.phis.empty()){altered=iteration;altered.phis[0].edge.value^=1;test::Check(std::holds_alternative<ForOfIterationFailure>(verify_iteration(altered)));}
    std::size_t ilow=0,ihigh=SemanticBudget{}.bytes;
    while(ilow<ihigh){const auto middle=ilow+(ihigh-ilow)/2;auto trial=advancing_build({middle,SemanticBudget{}.work});if(std::holds_alternative<ForOfIterationProof>(trial))ihigh=middle;else {test::Check(std::get<ForOfIterationFailure>(trial).kind==ForOfIterationError::kResourceLimit);ilow=middle+1;}}
    Take<ForOfIterationProof>(advancing_build({ilow,SemanticBudget{}.work}),"iteration-exact-bytes");
    test::Check(ilow>0 && std::get<ForOfIterationFailure>(advancing_build({ilow-1,SemanticBudget{}.work})).kind==ForOfIterationError::kResourceLimit);
    std::cout<<"advancing="<<iteration.iterations.size()<<" definitions="<<iteration.definitions.size()<<" phis="<<iteration.phis.size()<<" work="<<iteration.work<<'\n';
    for(auto contract:{kSourceContract,SourceContract::kLanguageSourceV1}) {
      auto refused=IndexForOfIdentities(body,graph,input,plan,control,owner,contract);
      test::Check(std::get<ForOfIdentityFailure>(refused).kind==ForOfIdentityError::kAcquisitionSemantics);
    }
    const auto reject=[&](ForOfIdentityIndex value){test::Check(std::holds_alternative<ForOfIdentityFailure>(verify(value)));};
    auto wrong=protocol;wrong.loops[0].iterator=wrong.loops[0].result;reject(std::move(wrong));
    wrong=protocol;wrong.loops[0].next=wrong.loops[0].advance;reject(std::move(wrong));
    wrong=protocol;wrong.loops[0].exhausted.successor^=1;reject(std::move(wrong));
    wrong=protocol;wrong.operations[wrong.loops[0].check.value].reset();reject(std::move(wrong));
    wrong=protocol;wrong.uses.clear();reject(std::move(wrong));
    wrong=protocol;wrong.values[wrong.loops[0].iterator.value].reset();reject(std::move(wrong));
    const auto build=[&](SemanticBudget b){return IndexForOfIdentities(body,graph,input,plan,control,owner,SourceContract::kLanguageSourceV4,b);};
    Take<ForOfIdentityIndex>(build({SemanticBudget{}.bytes,protocol.work}),"exact-work");
    auto low_work=build({SemanticBudget{}.bytes,protocol.work-1});
    test::Check(std::get<ForOfIdentityFailure>(low_work).kind==ForOfIdentityError::kWorkLimit);
    std::size_t lo=0,hi=SemanticBudget{}.bytes;
    while(lo<hi){const auto mid=lo+(hi-lo)/2;auto trial=build({mid,SemanticBudget{}.work});
      if(std::holds_alternative<ForOfIdentityIndex>(trial))hi=mid;
      else {test::Check(std::get<ForOfIdentityFailure>(trial).kind==ForOfIdentityError::kResourceLimit);lo=mid+1;}}
    Take<ForOfIdentityIndex>(build({lo,SemanticBudget{}.work}),"exact-bytes");
    test::Check(lo>0 && std::get<ForOfIdentityFailure>(build({lo-1,SemanticBudget{}.work})).kind==ForOfIdentityError::kResourceLimit);
    std::cout<<"padding="<<padding<<" loops="<<protocol.loops.size()<<" work="<<protocol.work<<'\n';
  }catch(const Refused&){test::Check(false);}
}
int main(int argc,char**argv) {
  test::Check(argc>=2 && argc<=4);
#ifdef ABCD_TEST_FOR_OF_DRAFT
  if(argc==4 && std::string_view(argv[3]).starts_with("--output="))draft_output=std::string_view(argv[3]).substr(std::string_view("--output=").size());
#endif
  if(argc==4 && std::string_view(argv[3])=="--variants") {Case(argv[1],0,std::stoul(argv[2]),1);Case(argv[1],0,std::stoul(argv[2]),0,true);return 0;}
  if(argc==4 && std::string_view(argv[3])=="--scale") {for(unsigned padding:{200U,201U,400U,800U})Case(argv[1],padding,std::stoul(argv[2]));return 0;}
  if(argc>=3)Case(argv[1],0,std::stoul(argv[2]),0,false,argc==4 && std::string_view(argv[3])=="--expect-ownership-gap",argc==4 && std::string_view(argv[3])=="--protocol-only");
  else {for(unsigned padding:{0,200,201,400,800})Case(argv[1],padding);Case(argv[1],0,451,1);Case(argv[1],0,451,0,true);}
}
