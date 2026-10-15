#include "../../../src/source/creator_classes.h"
#include "../../../src/source/sendable_class.h"
#include "abcd/corpus/abc_file.h"
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
void Check(bool v,std::source_location at=std::source_location::current()) {if(!v){std::cerr<<at.line()<<": Sendable proof failed\n";std::abort();}}
template<class T,class... V>T Take(std::variant<V...> v,std::source_location at=std::source_location::current()) {
  if(auto* t=std::get_if<T>(&v))return std::move(*t);
  std::cerr<<at.line()<<": variant "<<v.index()<<'\n';std::abort();
}
int main(int argc,char** argv) {
  Check(argc==2);
  for(unsigned mask=0;mask<256;++mask)Check(PrimitiveSendableType({static_cast<std::uint8_t>(mask)})==(mask && !(mask&40)));
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  const auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles=Take<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file,header,index,lookup));
  const auto functions=Take<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file,header,index,lookup));
  auto build=[&](analysis::SemanticBudget b=analysis::SemanticBudget{}){return analysis::ClassMethodSites::Build(*file,header,index,lookup,b);};
  const auto classes=Take<analysis::ClassMethodSites>(build());
  std::size_t lo=0,hi=analysis::SemanticBudget{}.bytes;
  while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<analysis::ClassMethodSites>(build({mid})))hi=mid;else lo=mid+1;}
  Check(lo>0 && std::holds_alternative<analysis::ClassMethodSites>(build({lo,classes.Work()})));
  Check(!std::holds_alternative<analysis::ClassMethodSites>(build({lo-1,classes.Work()})));
  Check(!std::holds_alternative<analysis::ClassMethodSites>(build({lo,classes.Work()-1})));
  auto identity=MakeRouteInputIdentity(hash,header);identity.contract=SourceContract::kLanguageSourceV3;
  std::optional<abc::MethodPosition> entry;
  for(std::size_t i=0;i<index.methods.size();++i)if(index.methods[i].name=="func_main_0")entry=abc::MethodPosition{static_cast<std::uint32_t>(i)};
  Check(entry.has_value());
  auto function=Take<FileFunction>(ReadFunctionValues(*file,header,index,*entry,identity,InputCaptureMode::kConditional));
  const CreatorClassInput input{*file,header,index,lookup,roles,functions,classes,function,identity,{index.methods[entry->value].offset,{0},{0}}};
  auto plan=Take<CreatorClasses>(DiscoverCreatorClasses(input));
  Check(plan.tasks.size()==1);
  Take<CreatorClassCost>(VerifyCreatorClasses(input,plan));
  auto& tree=*plan.tasks.front().tree;Check(tree.sendable!=nullptr);
  Check(!VerifySendableClass(tree,false));
  const auto original=tree.sendable;
  auto forged=std::make_shared<analysis::IndexedSendableLiteral>(*original);
  forged->fields.fields.front().type.bits=8;tree.sendable=forged;
  Check(VerifySendableClass(tree,false)->kind==SendableError::kTypeSpelling);
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));
  tree.sendable=original;
  forged=std::make_shared<analysis::IndexedSendableLiteral>(*original);
  forged->field_names.front().text=u"constructor";tree.sendable=forged;
  Check(VerifySendableClass(tree,false)->kind==SendableError::kTypeSpelling);tree.sendable=original;
  forged=std::make_shared<analysis::IndexedSendableLiteral>(*original);
  forged->field_failure=abc::SendableFieldFailure{abc::SendableFieldError::kMask,0};tree.sendable=forged;
  Check(VerifySendableClass(tree,false)->kind==SendableError::kFieldTypes);tree.sendable=original;
  const auto literal=tree.creation.creation.literal;++tree.creation.creation.literal.value;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));tree.creation.creation.literal=literal;
  const auto target=tree.creation.target;++tree.creation.target.value;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));tree.creation.target=target;
  auto& operation=std::get<analysis::ValueClassCreation>(function.values.graph.ops[plan.tasks.front().operation.value].operation);
  operation.kind=abc::ClassKind::kOrdinary;
  Check(std::holds_alternative<CreatorClassFailure>(DiscoverCreatorClasses(input)));operation.kind=abc::ClassKind::kSendable;
  const auto contract=tree.identity.contract;tree.identity.contract=SourceContract::kLanguageSourceV2;
  Check(VerifySendableClass(tree,false)->kind==SendableError::kOutputPolicy);tree.identity.contract=contract;
  Check(VerifySendableClass(tree,true)->kind==SendableError::kPreparationGate);
  Take<CreatorClassCost>(PrepareCreatorClasses(input,plan,nullptr,{10},RecoveryMode::kCompleteOnly));
  Take<CreatorClassCost>(FinishCreatorClasses(input,plan,nullptr,{10},nullptr,RecoveryMode::kCompleteOnly));
  Check(!VerifySendableClass(tree,true));
  const auto& family=tree.constructor->Family();
  auto& root=const_cast<FunctionTree&>(family.members[family.root.value].tree);
  const auto directive=root.sendable;root.sendable.reset();
  Check(VerifySendableClass(tree,true)->kind==SendableError::kCorrespondence);root.sendable=directive;
  ++root.sendable->creator.value;Check(VerifySendableClass(tree,true)->kind==SendableError::kCorrespondence);root.sendable=directive;
  root.modules.push_back({});Check(VerifySendableClass(tree,true)->kind==SendableError::kEnvironment);root.modules.pop_back();
  root.captures.push_back({});Check(VerifySendableClass(tree,true)->kind==SendableError::kCapture);root.captures.pop_back();
  root.recovery_issues.push_back({});Check(VerifySendableClass(tree,true)->kind==SendableError::kPreparationGate);root.recovery_issues.pop_back();
  if(!tree.members.empty()) {
    const auto& member=tree.members.front()->Family();auto& body=const_cast<FunctionTree&>(member.members[member.root.value].tree);
    const auto method=body.method;++body.method.value;Check(VerifySendableClass(tree,true)->kind==SendableError::kCorrespondence);body.method=method;
  }
  std::uint64_t proof_work=0;Check(!VerifySendableClass(tree,true,{},&proof_work));Check(proof_work>0);
  Check(!VerifySendableClass(tree,true,{0,proof_work}));
  Check(VerifySendableClass(tree,true,{0,proof_work-1})->kind==SendableError::kWorkLimit);
  std::cout<<"fields="<<original->fields.fields.size()<<" instance="<<original->fields.instance_count
      <<" index_boundary="<<lo<<'/'<<classes.Work()<<" wrong-kind/pool/creator/mask/unfinished mutations rejected\n";
}
