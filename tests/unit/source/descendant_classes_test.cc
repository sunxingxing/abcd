#include "../../../src/source/descendant_classes.h"
#include "../../../src/source/family_route_failure.h"
#include "../../../src/source/runtime_name.h"
#include "abcd/corpus/abc_file.h"
#include <filesystem>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool b,std::source_location at=std::source_location::current()){
  if(!b){std::cerr<<"descendant classes "<<at.line()<<'\n';std::abort();}
}
template<class T,class... E>T Take(std::variant<E...> v,std::source_location at=std::source_location::current()){
  if(auto* t=std::get_if<T>(&v))return std::move(*t);
  std::cerr<<"descendant classes variant "<<at.line()<<'\n';
  std::visit([](const auto& e){if constexpr(!std::is_same_v<std::decay_t<decltype(e)>,T>){FailureCauseChain chain;CollectFailureCauses(e,chain);for(const auto& c:chain.View())std::cerr<<c.domain<<':'<<c.value<<'\n';}},v);
  std::abort();
}
}
int main(int argc,char**argv){
  Check(argc==2);
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(std::filesystem::path(argv[1])/"closed.abc"));
  auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto roles=Take<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file,header,index,lookup));
  auto functions=Take<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file,header,index,lookup));
  auto classes=Take<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(*file,header,index,lookup));
  auto identity=MakeRouteInputIdentity(hash,header);identity.contract=SourceContract::kLanguageSourceV2;
  std::optional<analysis::FunctionSiteId> site;
  for(std::size_t i=0;i<index.methods.size();++i)if(RuntimeName(index.methods[i])=="factory"){
    auto found=functions.Find(index.methods[i].offset);if(found.count==1)site=found.first;
  }
  Check(site.has_value());
  FunctionContext context{Take<CreationTreeContext>(CreationTreeContext::Resolve(*file,header,index,lookup,roles,functions,*site,identity))};
  const auto read=[&]{return Take<FileFamily>(ReadFunctionFamily(*file,header,index,lookup,roles,functions,context.Position(),identity,
      {},nullptr,&context,{},FamilyClassMode::kReject,nullptr,{},nullptr,&classes));};
  auto source=read();
  auto prepared=Take<DescendantClasses>(PrepareDescendantClasses(*file,header,index,lookup,source,false,nullptr,RecoveryMode::kCompleteOnly));
  Check(prepared.creators.size()==source.members.size() && prepared.creators[0]);
  auto& plan=*prepared.creators[0];
  const auto& function=source.members[0].function;
  const CreatorClassInput input{*file,header,index,lookup,roles,functions,classes,function.Normal(),identity,
      {function.Method(),source.first_owner,{0}},function.Storage()};
  const auto first=FunctionOwnerId{static_cast<std::uint32_t>(source.members.size())};
  Check(std::holds_alternative<CreatorClassFailure>(VerifyFinishedClassOwners(plan,first)));
  const GlobalScope scope{}; // Explicit empty outer lexical enclosure, not an assumed one.
  auto tree_input=Take<FamilyTreeInput>(FamilyTreeInput::Prepare(*file,header,index,read(),context,{},nullptr,{},&lookup,
      RecoveryMode::kCompleteOnly,nullptr,nullptr,&scope));
  auto trees=Take<ConstructedFamily>(std::move(tree_input).Build(*file,header,index,context,{},nullptr,{},&lookup,
      RecoveryMode::kCompleteOnly,nullptr,&scope));
  Check(trees.writes.has_value());
  Take<FamilyWrites>(VerifyFamilyWrites(index,source,trees,{},&lookup));
  const auto original=source.members[0].classes[0];
  ++source.members[0].classes[0].creation.value;
  Check(std::holds_alternative<FamilyWriteFailure>(VerifyFamilyWrites(index,source,trees,{},&lookup)));
  source.members[0].classes[0]=original;
  std::shared_ptr<ClassTree> definition;
  for(auto& node:trees.members[0].tree.expressions)if(auto* c=std::get_if<ClassExpr>(&node.expression))definition=std::const_pointer_cast<ClassTree>(c->definition);
  Check(definition && definition->DraftPrepared());
  plan.tasks[0].tree=definition;
  Take<CreatorClassCost>(VerifyFinishedClassOwners(plan,first));
  const auto creator=definition->creation.creator;++definition->creation.creator.value;
  Check(std::holds_alternative<FamilyWriteFailure>(VerifyFamilyWrites(index,source,trees,{},&lookup)));
  definition->creation.creator=creator;
  const auto pool=definition->creation.creation.literal;++definition->creation.creation.literal.value;
  // Layout identity is checked against original pool references at discovery.
  auto discovery=Take<CreatorClasses>(DiscoverCreatorClasses(input));
  discovery.tasks[0].tree->creation.creation.literal=definition->creation.creation.literal;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,discovery)));
  definition->creation.creation.literal=pool;
  const auto range=plan.tasks[0].owners;++plan.tasks[0].owners->begin;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyFinishedClassOwners(plan,first)));
  plan.tasks[0].owners=range;
  auto constructor=std::move(definition->constructor);definition->constructor.reset();
  Check(std::holds_alternative<CreatorClassFailure>(VerifyFinishedClassOwners(plan,first)));
  Check(std::holds_alternative<FamilyWriteFailure>(VerifyFamilyWrites(index,source,trees,{},&lookup)));
  definition->constructor=std::move(constructor);
  Take<FamilyWrites>(VerifyFamilyWrites(index,source,trees,{},&lookup));
  const CaptureTree parent{trees.members[0].tree,trees.members[0].flow};
  ++plan.occurrence.member.value;
  Check(std::holds_alternative<CreatorClassFailure>(FinishCreatorClasses(input,plan,nullptr,first,&parent,RecoveryMode::kCompleteOnly)));
  std::cout<<"descendant_classes_test: production tree, original barrier, creator/pool/owner/completion mutations\n";
}
