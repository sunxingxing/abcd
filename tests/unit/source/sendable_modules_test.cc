#include "../../../src/source/creator_classes.h"
#include "../../../src/source/sendable_types.h"
#include "../../../src/source/sendable_class.h"
#include "../../../src/source/shared_module.h"
#include "../../../src/source/module_reads.h"
#include "abcd/source/global_reference.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/corpus/abc_file.h"
#include <iostream>
#include <source_location>
#include <sstream>
using namespace abcd;using namespace abcd::source;
void Check(bool v,std::source_location at=std::source_location::current()){if(!v){std::cerr<<at.line()<<": failed\n";std::abort();}}
template<class T,class... V>T Take(std::variant<V...> v,std::source_location at=std::source_location::current()){if(auto* t=std::get_if<T>(&v))return std::move(*t);std::cerr<<at.line()<<": variant "<<v.index()<<'\n';std::abort();}
int main(int argc,char** argv){
  Check(argc==2||argc==3);auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto roles=Take<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file,header,index,lookup));auto functions=Take<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file,header,index,lookup));auto classes=Take<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(*file,header,index,lookup));
  auto identity=MakeRouteInputIdentity(hash,header);identity.contract=SourceContract::kLanguageSourceV3;
  std::optional<abc::MethodPosition> entry;for(std::size_t i=0;i<index.methods.size();++i)if(index.methods[i].name=="func_main_0")entry=abc::MethodPosition{static_cast<std::uint32_t>(i)};
  Check(entry.has_value());const auto recordId=index.methods[entry->value].owner_offset;const abc::RecordInfo* record=nullptr;for(const auto& r:index.records)if(r.offset==recordId)record=&r;Check(record);
  auto context=Take<RecordModuleContext>(ReadRecordModuleContext(*file,header,index,*record,Take<abc::FilePackaging>(abc::DetectFilePackaging(*file,header,index))));
  auto enclosure=Take<ModuleEnclosure>(ModuleEnclosure::Build(*file,header,context,identity,{recordId.value}));GlobalScope globals;globals.module_enclosure=&enclosure;
  auto function=Take<FileFunction>(ReadFunctionValues(*file,header,index,*entry,identity,InputCaptureMode::kConditional,{},&context));
  CreatorClassInput input{*file,header,index,lookup,roles,functions,classes,function,identity,{index.methods[entry->value].offset,{0},{0}}};
  auto plan=Take<CreatorClasses>(DiscoverCreatorClasses(input));Check(plan.tasks.size()==1);
  Take<CreatorClassCost>(PrepareCreatorClasses(input,plan,&globals,{10},RecoveryMode::kCompleteOnly));
  Take<CreatorClassCost>(FinishCreatorClasses(input,plan,&globals,{10},nullptr,RecoveryMode::kCompleteOnly));auto& tree=*plan.tasks[0].tree;
  Check(!tree.sendable_types.empty());Check(!VerifySendableClass(tree,true));
  const auto proof=Take<SendableTypeCost>(VerifySendableTypes(tree));Check(proof.allocated_bytes>0&&proof.work>0);
  Check(std::holds_alternative<SendableTypeCost>(VerifySendableTypes(tree,{proof.allocated_bytes,proof.work})));
  Check(std::get<SendableFailure>(VerifySendableTypes(tree,{proof.allocated_bytes-1,proof.work})).kind==SendableError::kResourceLimit);
  Check(std::get<SendableFailure>(VerifySendableTypes(tree,{proof.allocated_bytes,proof.work-1})).kind==SendableError::kWorkLimit);
  auto original=tree.sendable_types;auto wrong=[&]{Check(std::holds_alternative<SendableFailure>(VerifySendableTypes(tree)));tree.sendable_types=original;};
  ++tree.sendable_types.front().binding.symbol.value;wrong();++tree.sendable_types.front().field;wrong();++tree.sendable_types.front().constructor.value;wrong();++tree.sendable_types.front().assignment.value;wrong();++tree.sendable_types.front().depth;wrong();tree.sendable_types.pop_back();wrong();tree.sendable_types.push_back(original.front());wrong();
  auto& body=const_cast<FunctionTree&>(tree.constructor->Family().members[tree.constructor->Family().root.value].tree);
  Take<ModuleDestination>(VerifyModuleDestination(body,recordId,&globals));
  Check(!body.modules.empty());
  const auto original_binding=body.modules.front().target;
  body.modules.front().target.symbol.value=static_cast<std::uint32_t>(enclosure.Names().size());
  Check(std::holds_alternative<ModuleEnclosureError>(VerifyModuleDestination(body,recordId,&globals)));
  body.modules.front().target=original_binding;
  bool mutated=false;for(auto& node:body.expressions)if(auto* read=std::get_if<SymbolExpr>(&node.expression);read&&read->domain==analysis::ModuleDomain::kSendable){
    read->domain=analysis::ModuleDomain::kOrdinary;Check(std::holds_alternative<ModuleEnclosureError>(VerifyModuleDestination(body,recordId,&globals)));Check(std::holds_alternative<SendableFailure>(VerifySendableTypes(tree)));read->domain=analysis::ModuleDomain::kSendable;mutated=true;break;}
  Check(mutated);
  const auto domain=body.module_domain;body.module_domain=analysis::ModuleDomain::kOrdinary;Check(VerifySendableClass(tree,true)->kind==SendableError::kEnvironment);body.module_domain=domain;
  body.captures.push_back({});Check(VerifySendableClass(tree,true)->kind==SendableError::kCapture);body.captures.pop_back();
  const auto module=context.ForOwner(*file,recordId);analysis::ValueProgram graph;analysis::ValueOp op;op.life=analysis::ValueLife::kLive;
  op.operation=analysis::ValueModuleRead{analysis::ModuleSpace::kExternal,{0},analysis::ModuleLoading::kOrdinary,analysis::ModuleDomain::kSendable};graph.ops.push_back(op);
  auto resolved=Take<ModuleReads>(ResolveModuleReads(*file,header,module.get(),graph,{},identity.contract));Check(resolved.bindings.size()==1);Check(std::holds_alternative<ImportedModuleBinding>(resolved.bindings[0].target));
  Check(std::get<ModuleReadError>(std::get<ModuleReadFailure>(ResolveModuleReads(*file,header,module.get(),graph,{},SourceContract::kLanguageSourceV2)).reason)==ModuleReadError::kContract);
  std::get<analysis::ValueModuleRead>(graph.ops[0].operation).slot={65535};Check(std::get<ModuleReadError>(std::get<ModuleReadFailure>(ResolveModuleReads(*file,header,module.get(),graph,{},identity.contract)).reason)==ModuleReadError::kSlot);
  Check(std::get<SharedModuleFailure>(SharedModuleProof::Build(context,*file,recordId,identity.contract)).kind==SharedModuleError::kAttributes);
  Check(std::get<SharedModuleFailure>(SharedModuleProof::Build(context,*file,{recordId.value+1},identity.contract)).kind==SharedModuleError::kOwner);
  if(argc==3){
    auto shared_file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[2]));
    auto fingerprint=Take<corpus::FileFingerprint>(shared_file->Fingerprint());
    auto shared_header=Take<abc::Header>(abc::ParseHeader(std::span(fingerprint.prefix).first(fingerprint.prefix_size),fingerprint.size));
    auto metadata=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*shared_file,shared_header));
    bool checked=false;
    for(const auto& r:metadata.records){
      auto ctx=Take<RecordModuleContext>(ReadRecordModuleContext(*shared_file,shared_header,metadata,r,abc::FilePackaging::kMerged));
      if(!ctx.ForOwner(*shared_file,r.offset))continue;
      const auto valid=Take<SharedModuleProof>(SharedModuleProof::Build(ctx,*shared_file,r.offset,identity.contract));
      Check(valid.Matches(ctx,*shared_file,identity.contract));
      auto shared_identity=MakeRouteInputIdentity(fingerprint,shared_header);shared_identity.contract=identity.contract;
      auto imports=Take<ModuleEnclosure>(ModuleEnclosure::Build(*shared_file,shared_header,ctx,shared_identity,{r.offset.value}));
      Check(imports.Shared());std::ostringstream text;
      const auto bytes=Take<std::size_t>(imports.WriteImports(*shared_file,shared_header,text));
      Check(bytes==text.str().size() && text.str().starts_with("\"use shared\";\n"));
      std::ostringstream exact,short_output;
      Check(Take<std::size_t>(imports.WriteImports(*shared_file,shared_header,exact,{bytes}))==bytes);
      Check(std::get<ModuleImportFailure>(imports.WriteImports(*shared_file,shared_header,short_output,{bytes-1})).kind==ModuleImportError::kOutputLimit);

      Check(!valid.Matches(ctx,*shared_file,SourceContract::kLanguageSourceV2));
      Check(!valid.Matches(context,*file,identity.contract));
      Check(std::get<SharedModuleFailure>(SharedModuleProof::Build(ctx,*shared_file,{r.offset.value+1},identity.contract)).kind==SharedModuleError::kOwner);
      checked=true;
    }
    Check(checked);
  }
  std::cout<<"types="<<tree.sendable_types.size()<<" budget="<<proof.allocated_bytes<<'/'<<proof.work<<'\n';
}
