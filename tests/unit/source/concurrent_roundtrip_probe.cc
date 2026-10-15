// Test boundary: inspect the actual recompilation, never the generated text.
#include <iostream>
#include <nlohmann/json.hpp>
#include "abcd/abc/concurrent_requests.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/abc/record_attributes.h"
#include "abcd/abc/string_entity.h"
#include "abcd/corpus/abc_file.h"
#include "../../../src/source/runtime_name.h"
#include "../../../src/source/concurrent_completion.h"
#include "../../../src/source/concurrent_async.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/exception_modules.h"
#include "abcd/abc/call_type.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
template<class T,class...U>T Take(std::variant<U...> v){if(auto*p=std::get_if<T>(&v))return std::move(*p);std::cerr<<"variant failure "<<typeid(T).name()<<" index "<<v.index()<<'\n';std::abort();}
// Narrow TEST oracle for recompilations containing an otherwise unsupported
// pre-notification operation (e.g. STOWNBYVALUEWITHNAMESET). No source admission:
// register SSA proves payload identity, dominators prove notification-before-
// return, and a complete pure terminating suffix proves event ordering. The
// oracle deliberately refuses multiple physical completion ports or handlers.
bool RawSuffix(const abc::MethodBody& body,abc::ProfileId profile,const abc::FrameLayout& frame) {
 if(!body.handlers.empty())return false;
 const auto flow=Take<analysis::NormalFlow>(analysis::BuildNormalFlow(body));
 const auto dom=Take<analysis::DominatorTree>(analysis::BuildNormalDominators(flow));
 analysis::IdfQuota quota{analysis::kDefaultSemanticWork};
 std::vector<analysis::RegisterSite> sites;
 for(const auto& instruction:body.instructions) {
  auto lifted=analysis::LiftRegisterEffect(instruction.decoded,frame.registers);
  analysis::RegisterEffect effect;
  if(const auto* known=std::get_if<analysis::RegisterEffect>(&lifted))effect=*known;
  else {
   // Decoder's pinned ISA register read/write metadata suffices for this
   // TEST-only accumulator identity oracle. It grants no heap/source meaning.
   const auto& d=instruction.decoded;
   if(d.accumulator_read)effect.reads[effect.read_count++]=analysis::Accumulator{};
   if(d.accumulator_write)effect.write=analysis::Accumulator{};
   for(unsigned i=0;i<d.operand_count;++i) {
    const auto& operand=d.operands[i];if(operand.kind!=abc::OperandKind::kRegister)continue;
    if(operand.raw_bits>=frame.registers)return false;
    const abc::RegisterId reg{static_cast<std::uint32_t>(operand.raw_bits)};
    if(operand.access!=abc::OperandAccess::kWrite) {
     if(effect.read_count==effect.reads.size())return false;
     effect.reads[effect.read_count++]=reg;
    }
    if(operand.access!=abc::OperandAccess::kRead) {if(effect.write)return false;effect.write=reg;}
   }
  }
  sites.push_back({instruction.offset,effect});
 }
 auto renamed=analysis::RegisterSsa::BuildSites(sites,flow,frame.registers,quota);
 if(!std::holds_alternative<analysis::RegisterSsa>(renamed))return false;
 const auto& ssa=std::get<analysis::RegisterSsa>(renamed);
 std::vector<std::vector<std::uint32_t>> successors(body.instructions.size());
 std::vector<analysis::BlockId> blocks(body.instructions.size());
 std::optional<std::uint32_t> notify,returned;
 for(std::uint32_t b=0;b<flow.blocks.size();++b) {
  const auto& block=flow.blocks[b];
  for(std::uint32_t i=block.first_instruction;i<block.first_instruction+block.instruction_count;++i) {
   blocks[i]={b};
   if(i+1<block.first_instruction+block.instruction_count)successors[i].push_back(i+1);
   else for(unsigned e=0;e<block.successor_count;++e)successors[i].push_back(flow.blocks[block.successors[e].target.value].first_instruction);
   if(!dom.nodes[b].begin)continue;
   auto lifted=analysis::LiftSemanticInstruction(body.instructions[i],profile,frame.registers);
   if(const auto* op=std::get_if<analysis::SemanticInstruction>(&lifted)) {
    if(std::holds_alternative<analysis::ConcurrentNotification>(op->operation)){if(notify)return false;notify=i;}
    if(std::holds_alternative<analysis::ReturnOperation>(op->operation)){if(returned)return false;returned=i;}
   }
  }
 }
 if(!notify||!returned||!dom.Dominates(blocks[*notify],blocks[*returned])||
    (blocks[*notify]==blocks[*returned]&&*notify>=*returned))return false;
 const auto a=ssa.Reads({*notify}),b=ssa.Reads({*returned});
 if(a.size()!=1||b.size()!=1||a[0].value!=b[0].value)return false;
 std::vector<unsigned char> color(body.instructions.size());
 std::vector<std::pair<std::uint32_t,bool>> stack;
 for(auto next:successors[*notify])stack.push_back({next,false});
 bool reached=false;
 while(!stack.empty()) {
  auto [i,finish]=stack.back();stack.pop_back();
  if(finish){color[i]=2;continue;}
  if(color[i]==1)return false;if(color[i]==2)continue;color[i]=1;
  if(i==*returned){reached=true;color[i]=2;continue;}
  auto lifted=analysis::LiftSemanticInstruction(body.instructions[i],profile,frame.registers);
  const auto* op=std::get_if<analysis::SemanticInstruction>(&lifted);
  if(!op||op->effects.normal!=analysis::LanguageEffect::kNone||op->effects.before_throw!=analysis::LanguageEffect::kNone||op->effects.exceptional!=analysis::ExceptionalCompletion::kImpossible||successors[i].empty())return false;
  stack.push_back({i,true});for(auto next:successors[i])stack.push_back({next,false});
 }
 return reached;
}
int main(int argc,char**argv) {
 test::Check(argc==2 || argc==3);
 auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
 auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
 auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
 auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
 if(argc==3) {
  const auto entity=static_cast<std::uint32_t>(std::stoul(argv[2]));
  const auto m=std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& method){return method.offset.value==entity;});test::Check(m!=index.methods.end());
  const auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*m->code));
  const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,m->annotations));
  const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(m->code->registers,m->code->arguments,abc::DecodeCallFlags(call.value)));
  std::vector<abc::CodeOffset> origins;std::vector<analysis::AwaitSite> awaits;
  auto normalized=Take<abc::MethodBody>(source::ProveAsyncCompletion(body,frame,analysis::SemanticBudget{}.bytes,&origins,&awaits,header.profile,source::ResourceFailureScope::kExcluded));
  const auto flow=Take<analysis::ExceptionFlow>(analysis::BuildExceptionFlow(normalized,header.profile,frame.registers,{},awaits,&frame));
  auto environments=analysis::AnalyzeExceptionEnvironments(flow);
  const auto* error=std::get_if<analysis::EnvironmentFlowFailure>(&environments);test::Check(error!=nullptr);
  const auto site=std::lower_bound(normalized.instructions.begin(),normalized.instructions.end(),error->offset,[](const auto& instruction,auto pc){return instruction.offset<pc;});
  test::Check(site!=normalized.instructions.end()&&site->offset==error->offset);
  nlohmann::json diagnostic{{"method",entity},{"environment_error",unsigned(error->kind)},{"working_pc",error->offset.value},{"pc",origins[site-normalized.instructions.begin()].value}};
  if(error->environment_error)diagnostic["environment_cause"]=unsigned(*error->environment_error);
  if(error->lexical_error)diagnostic["lexical_cause"]=unsigned(*error->lexical_error);
  diagnostic["environment_changes"]=nlohmann::json::array();diagnostic["handler_incoming"]=nlohmann::json::array();
  const auto raw_pc=[&](abc::CodeOffset pc){auto found=std::lower_bound(normalized.instructions.begin(),normalized.instructions.end(),pc,[](const auto& op,auto target){return op.offset<target;});test::Check(found!=normalized.instructions.end()&&found->offset==pc);return origins[found-normalized.instructions.begin()].value;};
  for(const auto& instruction:flow.instructions) {
    if(std::holds_alternative<analysis::PushEnvironment>(instruction.operation)||std::holds_alternative<analysis::PopEnvironment>(instruction.operation))
      diagnostic["environment_changes"].push_back({{"push",std::holds_alternative<analysis::PushEnvironment>(instruction.operation)},{"pc",raw_pc(instruction.pc)}});
  }
  for(const auto& node:flow.nodes)for(unsigned e=0;e<node.successor_count;++e) {
    const auto& edge=node.successors[e];const auto& target=flow.nodes[edge.target.value];
    if(target.phase==analysis::ExceptionPhase::kCatch && flow.instructions[target.instruction.value].pc==error->offset)
      diagnostic["handler_incoming"].push_back({{"pc",raw_pc(flow.instructions[node.instruction.value].pc)},{"phase",unsigned(node.phase)},{"arc",unsigned(edge.kind)}});
  }
  std::cout<<diagnostic.dump()<<'\n';return 0;
 }
 auto text=[&](abc::EntityOffset id){auto value=Take<std::u16string>(abc::ReadStringEntity(*file,header,id));for(auto c:value)test::Check(c<128);return std::string(value.begin(),value.end());};
 nlohmann::json records=nlohmann::json::array();
 for(const auto& r:index.records) {
  auto attrs=Take<abc::RecordAttributes>(abc::ReadMergedRecordAttributes(*file,header,index,r));
  if(!attrs.module)continue;
  auto module=Take<abc::ModuleInfo>(abc::ReadModuleInfo(*file,header,*attrs.module,{},attrs.request_phases));
  nlohmann::json j{{"record",r.descriptor},{"requests",nlohmann::json::array()},{"imports",nlohmann::json::array()},{"exports",nlohmann::json::array()},{"namespace_imports",nlohmann::json::array()},{"indirect_exports",nlohmann::json::array()},{"star_exports",nlohmann::json::array()},{"functions",nlohmann::json::array()}};
  j["request_phases"]=nlohmann::json::array();
  for(std::size_t q=0;q<module.requests.size();++q){j["requests"].push_back(text(module.requests[q]));j["request_phases"].push_back(static_cast<unsigned>(module.request_phases.empty()?abc::ModuleRequestPhase::kEager:module.request_phases[q]));}
  for(const auto& i:module.imports)j["imports"].push_back({{"imported",text(i.imported)},{"local",text(i.local)},{"request",text(module.requests[i.request.value])}});
  for(const auto& e:module.local_exports)j["exports"].push_back({{"local",text(e.local)},{"exported",text(e.exported)}});
  for(const auto& i:module.namespace_imports)j["namespace_imports"].push_back({{"local",text(i.local)},{"request",text(module.requests[i.request.value])}});
  for(const auto& e:module.indirect_exports)j["indirect_exports"].push_back({{"exported",text(e.exported)},{"imported",text(e.imported)},{"request",text(module.requests[e.request.value])}});
  for(auto e:module.star_exports)j["star_exports"].push_back(text(module.requests[e.value]));
  for(std::size_t i=r.first_method;i<r.first_method+r.method_count;++i) {
   const auto& m=index.methods[i];auto a=Take<abc::MethodAttributes>(abc::DecodeMethodAttributes(m.access_flags,header.profile));
   if(a.kind!=abc::FunctionKind::kConcurrent)continue;
   auto requests=Take<abc::ConcurrentRequests>(abc::ReadConcurrentRequests(*file,header,index,m,r.offset,module.requests));
   auto body=Take<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*m.code));
   nlohmann::json f{{"method",m.offset.value},{"name",source::RuntimeName(m).value_or("")},{"kind",unsigned(a.kind)},{"sendable",a.sendable},{"annotation_present",requests.present},{"requests",nlohmann::json::array()},{"notifications",0},{"returns",0},{"handlers",body.handlers.size()}};
   for(const auto& q:requests.entries)f["requests"].push_back(text(q.specifier));
   bool asynchronous=false;
   for(const auto& instruction:body.instructions) {
    auto lifted=analysis::LiftSemanticInstruction(instruction,header.profile,std::uint64_t(m.code->registers)+m.code->arguments);
    if(const auto* s=std::get_if<analysis::SemanticInstruction>(&lifted)) {
     asynchronous|=std::holds_alternative<analysis::AsyncEnter>(s->operation);
     if(std::holds_alternative<analysis::ConcurrentNotification>(s->operation))f["notifications"]=f["notifications"].get<unsigned>()+1;
     if(std::holds_alternative<analysis::ReturnOperation>(s->operation))f["returns"]=f["returns"].get<unsigned>()+1;
    }
   }
   if(asynchronous){
    const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,m.annotations));
    const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(m.code->registers,m.code->arguments,abc::DecodeCallFlags(call.value)));
    auto completion=source::ProveAsyncNotifications(body,frame,header.profile);
    f["async_protocol"]=true;f["completion_verified"]=std::holds_alternative<source::AsyncNotifications>(completion);
    if(const auto* ports=std::get_if<source::AsyncNotifications>(&completion)) {
     f["resolve_ports"]=0;f["reject_ports"]=0;
     for(const auto& port:ports->ports) {
      auto key=port.kind==analysis::AsyncSettlement::kResolve?"resolve_ports":"reject_ports";
      f[key]=f[key].get<unsigned>()+1;
     }
     // Also require the existing async owner to prove Promise identity,
     // implicit catch/reject input, and resume regions. A failed owner stays
     // visible; notification suffix proof alone is never publication evidence.
     auto normalized=source::NormalizeConcurrentAsync(body,frame,header.profile);
     f["async_owner_verified"]=std::holds_alternative<source::ConcurrentAsyncBody>(normalized);
    }
    j["functions"].push_back(f);continue;
   }
   // Raw protocol-only recheck on the recompilation. Other unsupported root
   // operations (e.g. set-name own stores) do not hide method completion proof.
   const auto call=Take<abc::CallType>(abc::ReadCallType(*file,header,index,m.annotations));
   const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(m.code->registers,m.code->arguments,abc::DecodeCallFlags(call.value)));
   auto built=analysis::BuildExceptionFlow(body,header.profile,frame.registers,{}, {},&frame);
   if(!std::holds_alternative<analysis::ExceptionFlow>(built)) {
    f["completion_verified"]=RawSuffix(body,header.profile,frame);f["completion_probe"]="raw_ssa_suffix";j["functions"].push_back(f);continue;
   }
   const auto flow=Take<analysis::ExceptionFlow>(std::move(built));
   analysis::IdfQuota quota{analysis::kDefaultSemanticWork};
   const auto ssa=Take<analysis::ExceptionSsa>(analysis::ExceptionSsa::Build(flow,frame.registers,quota));
   std::vector<analysis::EntryValueFact> entries;
   // This low-level probe checks identity/completion only. Domain assumptions
   // here are not a source-admission certificate; engine publication rechecks
   // the callable/module/capture premises independently.
   for(std::size_t v=0;v<ssa.Values().size();++v)
    if(std::holds_alternative<analysis::MethodEntry>(ssa.Values()[v].origin)&&std::holds_alternative<abc::RegisterId>(ssa.Values()[v].slot))entries.push_back({{static_cast<std::uint32_t>(v)},analysis::ValueKinds::Language()});
   const auto environments=Take<analysis::EnvironmentFlow>(analysis::AnalyzeExceptionEnvironments(flow));
   const auto modules=Take<analysis::ExceptionModules>(analysis::ProveExceptionModules(flow,ssa));
   analysis::IdfQuota value_quota{analysis::kDefaultSemanticWork};
   const auto values=Take<analysis::ExceptionValueFlow>(analysis::ExceptionValueFlow::Build(flow,ssa,entries,value_quota,{},analysis::PendingExceptionValues::kLanguage,modules.loads, {},analysis::CallResultValues::kLanguage,analysis::FrameReceiver::kInitialized,analysis::ArithmeticSemantics::kSourceNumeric));
   const analysis::ArgumentFrameFact owner{frame};
   auto checked=analysis::VerifyExceptionInputs(flow,ssa,environments,values,analysis::kDefaultSemanticWork,&owner,analysis::ExceptionInputDomain::kHoleTransport);
   if(const auto* error=std::get_if<analysis::ExceptionInputFailure>(&checked)){std::cerr<<m.name<<" input "<<unsigned(error->kind)<<" pc"<<error->pc.value<<'\n';std::abort();}
   const auto proof=Take<analysis::ExceptionInputProof>(std::move(checked));
   const analysis::PhasedValueInput input{flow,ssa,environments,values,proof,frame};
   const auto graph=Take<analysis::ValueProgram>(analysis::ImportPhasedValues(input));
   auto completion=source::BuildConcurrentCompletion(graph);
   f["completion_verified"]=std::holds_alternative<source::ConcurrentCompletion>(completion);
   if(const auto* error=std::get_if<source::ConcurrentFailure>(&completion))f["completion_error"]=unsigned(error->kind);
   j["functions"].push_back(f);
  }
  records.push_back(j);
 }
 std::cout<<records.dump()<<'\n';
}
