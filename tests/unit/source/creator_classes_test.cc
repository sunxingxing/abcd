#include "../../../src/source/creator_classes.h"
#include "abcd/corpus/abc_file.h"
#include <algorithm>
#include "../../../src/source/runtime_name.h"
#include "../../../src/source/family_route_failure.h"
#include <filesystem>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value,std::source_location at=std::source_location::current()) {
  if(!value){std::cerr<<"creator classes: "<<at.line()<<'\n';std::abort();}
}
template<class T,class... U> T Take(std::variant<U...> v,std::source_location at=std::source_location::current()) {
  if(auto* value=std::get_if<T>(&v)) return std::move(*value);
  std::cerr<<"creator classes variant "<<at.line()<<" index="<<v.index()<<'\n';std::abort();
}
template<class T,class F> void Boundary(F&& build,std::uint64_t work) {
  const auto initial=analysis::SemanticBudget{};
  std::size_t low=0,high=initial.bytes;
  while(low<high){const auto mid=low+(high-low)/2;if(std::holds_alternative<T>(build({mid})))high=mid;else low=mid+1;}
  Check(low>0);Check(std::holds_alternative<T>(build({low,work})));
  Check(!std::holds_alternative<T>(build({low-1,work})));
  Check(!std::holds_alternative<T>(build({low,work-1})));
}
void Run(const std::filesystem::path& path,unsigned count,bool depth) {
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  const auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles=Take<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file,header,index,lookup));
  const auto functions=Take<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file,header,index,lookup));
  const auto classes=Take<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(*file,header,index,lookup));
  auto identity=MakeRouteInputIdentity(hash,header);identity.contract=SourceContract::kLanguageSourceV2;
  std::optional<abc::MethodPosition> creator,entry;
  const auto name=depth ? "f"+std::to_string(count-1) : "run";
  for(std::size_t i=0;i<index.methods.size();++i) {
    if(RuntimeName(index.methods[i])==name)creator=abc::MethodPosition{static_cast<std::uint32_t>(i)};
    if(index.methods[i].name=="func_main_0")entry=abc::MethodPosition{static_cast<std::uint32_t>(i)};
  }
  Check(creator && entry);
  auto function=Take<FileFunction>(ReadFunctionValues(*file,header,index,*creator,identity,InputCaptureMode::kConditional));
  const CreatorClassInput input{*file,header,index,lookup,roles,functions,classes,function,identity,{index.methods[entry->value].offset,{0},{depth?count:1}}};
  auto plan=Take<CreatorClasses>(DiscoverCreatorClasses(input));
  Check(plan.tasks.size()==(depth?1:count));
  const auto cost=Take<CreatorClassCost>(VerifyCreatorClasses(input,plan));
  Boundary<CreatorClasses>([&](analysis::SemanticBudget b){return DiscoverCreatorClasses(input,b);},plan.work);
  Boundary<CreatorClassCost>([&](analysis::SemanticBudget b){return VerifyCreatorClasses(input,plan,b);},cost.work);
  ++plan.occurrence.member.value;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));
  --plan.occurrence.member.value;
  auto& task=plan.tasks.front();
  Check(!task.preparation && !task.owners); // No guessed range during discovery.
  auto saved=task.tree->creation;
  ++task.tree->creation.creator.value;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));
  task.tree->creation=saved;++task.tree->creation.creation.literal.value;
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));
  task.tree->creation=saved;
  if(!depth) {
    const auto op=plan.tasks[1].operation;
    const auto tree=plan.tasks[1].tree;
    const auto original=function.values.classes.classes[1].operation;
    plan.tasks[1].operation=task.operation;plan.tasks[1].tree=task.tree;
    function.values.classes.classes[1].operation=task.operation;
    Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));
    plan.tasks[1].operation=op;plan.tasks[1].tree=tree;function.values.classes.classes[1].operation=original;
  }
  if(!depth) {
    auto removed=std::move(plan.tasks.back());plan.tasks.pop_back();
    const auto original=function.values.classes.classes.back();function.values.classes.classes.pop_back();
    Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClasses(input,plan)));
    function.values.classes.classes.push_back(original);plan.tasks.push_back(std::move(removed));
  }
  // A forged working pool is checked against the immutable semantic instruction.
  auto& operation=std::get<analysis::ValueClassCreation>(function.values.graph.ops[task.operation.value].operation);
  const auto literal=operation.literal;++operation.literal.value;
  Check(std::holds_alternative<CreatorClassFailure>(DiscoverCreatorClasses(input)));
  operation.literal=literal;
  Take<CreatorClassCost>(PrepareCreatorClasses(input,plan,nullptr,{31},RecoveryMode::kCompleteOnly));
  const auto owners=Take<CreatorClassCost>(VerifyCreatorClassOwners(plan,{31}));
  Check(std::holds_alternative<CreatorClassCost>(VerifyCreatorClassOwners(plan,{31},{0,owners.work})));
  Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClassOwners(plan,{31},{0,owners.work-1})));
  if(!depth) {
    const auto range=plan.tasks[1].owners;plan.tasks[1].owners=plan.tasks[0].owners;
    Check(std::holds_alternative<CreatorClassFailure>(VerifyCreatorClassOwners(plan,{31})));
    plan.tasks[1].owners=range;
  }
  Check(!ValidClassOwnerRange({31,31},31));Check(!ValidClassOwnerRange({31,std::uint64_t{UINT32_MAX}+2},31));
  // Exercise the production explicit family work stack, not a hand-built task list.
  const auto packaging=Take<abc::FilePackaging>(abc::DetectFilePackaging(*file,header,index));
  const auto record=std::find_if(index.records.begin(),index.records.end(),[&](const auto& r){return r.offset==index.methods[entry->value].owner_offset;});
  Check(record!=index.records.end());
  const auto modules=Take<RecordModuleContext>(ReadRecordModuleContext(*file,header,index,*record,packaging));
  const auto discover=[&](analysis::SemanticBudget budget) {
    return ReadFunctionFamily(*file,header,index,lookup,roles,functions,*entry,identity,budget,&modules,nullptr,{},
        FamilyClassMode::kRetainRoot,nullptr,{},nullptr,&classes);
  };
  const auto is_gate=[](const auto& result) {
    const auto* failure=std::get_if<FamilyFileFailure>(&result);
    return failure && failure->family_creation && failure->family_creation->reason==FamilyCreationCheck::kClassPreparationGate;
  };
  const auto result=discover({});
  const auto& failure=std::get<FamilyFileFailure>(result);
  Check(failure.family_creation && failure.family_creation->reason==FamilyCreationCheck::kClassPreparationGate);
  Check(failure.family_creation->location.method==index.methods[creator->value].offset);
  Check(failure.family_creation->occurrence->value==(depth?count:1));
  // Exact budgets for the real family work stack, including all preceding
  // creator occurrences. Gate is the successful result of this unpublished stage.
  std::size_t bytes_low=0,bytes_high=analysis::SemanticBudget{}.bytes;
  while(bytes_low<bytes_high) {
    const auto mid=bytes_low+(bytes_high-bytes_low)/2;
    if(is_gate(discover({mid}))) bytes_high=mid; else bytes_low=mid+1;
  }
  std::uint64_t work_low=0,work_high=1;
  while(!is_gate(discover({bytes_low,work_high}))) {Check(work_high<UINT64_MAX/2);work_high*=2;}
  while(work_low<work_high) {
    const auto mid=work_low+(work_high-work_low)/2;
    if(is_gate(discover({bytes_low,mid})))work_high=mid;else work_low=mid+1;
  }
  Check(is_gate(discover({bytes_low,work_low})));
  const auto byte_failure=Take<FamilyFileFailure>(discover({bytes_low-1,work_low}));
  const auto work_failure=Take<FamilyFileFailure>(discover({bytes_low,work_low-1}));
  Check(FamilyRouteFailure(FamilyPrepareFailure{byte_failure}).failure_class==RouteFailureClass::kMemoryLimit);
  Check(FamilyRouteFailure(FamilyPrepareFailure{work_failure}).failure_class==RouteFailureClass::kWorkLimit);
  std::cout<<"family_boundary="<<bytes_low<<'/'<<work_low<<' ';
  std::cout<<(depth?"depth":"width")<<count<<" tasks="<<plan.tasks.size()<<" discovery="<<plan.allocated_bytes<<'/'<<plan.work
      <<" verify="<<cost.allocated_bytes<<'/'<<cost.work<<'\n';
}
}
int main(int argc,char**argv) {
  Check(argc==2);
  for(auto count:{200U,201U,400U,800U}) for(bool depth:{false,true})
    Run(std::filesystem::path(argv[1])/((depth?"depth":"width")+std::to_string(count)+".abc"),count,depth);
}
