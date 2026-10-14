#include "../../../src/source/creator_classes.h"
#include "../../../src/source/runtime_name.h"
#include "../../../src/source/family_route_failure.h"
#include "abcd/corpus/abc_file.h"
#include <filesystem>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool b,std::source_location at=std::source_location::current()) {
  if(!b){std::cerr<<"creator requirements "<<at.line()<<'\n';std::abort();}
}
template<class T,class... U> T Take(std::variant<U...> value,std::source_location at=std::source_location::current()) {
  if(auto* found=std::get_if<T>(&value))return std::move(*found);
  std::cerr<<"creator requirements variant "<<at.line()<<" index "<<value.index()<<'\n';std::abort();
}
template<class T,class F> void Boundary(F&& f) {
  std::size_t lo=0,hi=analysis::SemanticBudget{}.bytes;
  while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<T>(f({mid})))hi=mid;else lo=mid+1;}
  Check(lo>0 && std::holds_alternative<T>(f({lo})) && !std::holds_alternative<T>(f({lo-1})));
  std::uint64_t a=0,b=1;
  while(!std::holds_alternative<T>(f({lo,b}))){Check(b<UINT64_MAX/2);b*=2;}
  while(a<b){const auto mid=a+(b-a)/2;if(std::holds_alternative<T>(f({lo,mid})))b=mid;else a=mid+1;}
  Check(a>0 && std::holds_alternative<T>(f({lo,a})) && !std::holds_alternative<T>(f({lo,a-1})));
  std::cout<<lo<<'/'<<a<<' ';
}
void Run(const std::filesystem::path& path,std::size_t count,SourceContract contract) {
  auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  const auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles=Take<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file,header,index,lookup));
  const auto functions=Take<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file,header,index,lookup));
  const auto classes=Take<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(*file,header,index,lookup));
  auto identity=MakeRouteInputIdentity(hash,header);identity.contract=contract;
  std::optional<abc::MethodPosition> creator;
  for(std::size_t i=0;i<index.methods.size();++i)
    if(RuntimeName(index.methods[i])=="factory")creator=abc::MethodPosition{static_cast<std::uint32_t>(i)};
  Check(creator.has_value());
  const auto occurrence=functions.Find(index.methods[creator->value].offset);
  Check(occurrence.count==1);
  FunctionContext context{Take<CreationTreeContext>(CreationTreeContext::Resolve(*file,header,index,lookup,roles,functions,occurrence.first,identity))};
  FileFamily family;
  auto read=ReadFunctionValues(*file,header,index,*creator,identity,InputCaptureMode::kConditional,{},nullptr,&context);
  if(auto* error=std::get_if<FunctionFileFailure>(&read)) {
    FailureCauseChain causes;CollectFailureCauses(*error,causes);
    for(const auto& cause:causes.View())std::cerr<<cause.domain<<':'<<cause.value<<'\n';
  }
  family.members.push_back({Take<FileFunction>(std::move(read))});
  auto& owner=family.members.front().function;
  const CreatorClassInput input{*file,header,index,lookup,roles,functions,classes,owner.Normal(),identity,
      {owner.Method(),{0},{0}}};
  Check(owner.Capture().OriginalInput().Captures().empty()); // Pure forwarding: no local lexical access.
  auto plan=Take<CreatorClasses>(DiscoverCreatorClasses(input));
  Take<CreatorClassCost>(PrepareCreatorClasses(input,plan,nullptr,{1},RecoveryMode::kCompleteOnly,{},ClassRootBinding::kRequirements));
  Check(plan.tasks.size()==count);
  ++plan.occurrence.member.value;
  Check(std::holds_alternative<CreatorClassFailure>(CollectCreatorClassDemands(input,plan,{0})));
  --plan.occurrence.member.value;
  Check(std::holds_alternative<CreatorClassFailure>(CollectCreatorClassDemands(input,plan,{1})));
  auto requirements=Take<CreatorClassDemands>(CollectCreatorClassDemands(input,plan,{0}));
  Check(!requirements.demands.empty());
  // No creator storage has been allocated for its incoming slot yet. A class
  // member requiring that slot must not bind against the raw FileFunction.
  Check(std::holds_alternative<CreatorClassFailure>(BindCreatorClasses(*file,header,index,lookup,owner.Normal(),nullptr,plan)));
  Take<FamilyCellCost>(PlanFamilyCells(family,{},requirements.demands));
  Check(owner.Storage() && !owner.Capture().Input().Captures().empty());
  Check(owner.Capture().OriginalInput().Captures().empty());
  for(const auto& use:owner.Capture().Input().Captures()) Check(use.domain==requirements.demands.front().demand.domain);
  Take<CreatorClassCost>(BindCreatorClasses(*file,header,index,lookup,owner.Normal(),owner.Storage(),plan));
  bool incoming=false,self=false;
  for(const auto& task:plan.tasks) for(const auto& body:task.preparation->bodies) {
    const auto& prepared=std::get<FamilyPreparation>(body);
    Check(!prepared.RequirementsOnly());
    const auto* captures=prepared.RootCapture();Check(captures!=nullptr);
    for(const auto& target:captures->targets) {
      const auto* parent=std::get_if<ParentCell>(&target.target);Check(parent!=nullptr);
      const auto& cell=owner.Storage()->cells.cells[parent->cell.value];
      incoming|=std::holds_alternative<IncomingCell>(cell.storage);
      self|=std::holds_alternative<AllocatedCell>(cell.storage);
    }
  }
  Check(incoming && !self);
  std::cout<<path.filename().string()<<" contract="<<static_cast<unsigned>(contract)<<" collect/cells/bind ";
  Boundary<CreatorClassDemands>([&](analysis::SemanticBudget b){return CollectCreatorClassDemands(input,plan,{0},b);});
  Boundary<FamilyCellCost>([&](analysis::SemanticBudget b){return PlanFamilyCells(family,b,requirements.demands);});
  // A failed planning attempt is not a committed family. Restore its full view.
  Take<FamilyCellCost>(PlanFamilyCells(family,{},requirements.demands));
  Boundary<CreatorClassCost>([&](analysis::SemanticBudget b){return BindCreatorClasses(*file,header,index,lookup,owner.Normal(),owner.Storage(),plan,b);});
  std::cout<<'\n';
  auto forged=*owner.Storage();++forged.cells.revision.value;
  Check(std::holds_alternative<CreatorClassFailure>(BindCreatorClasses(*file,header,index,lookup,owner.Normal(),&forged,plan)));
  auto changed=requirements.demands;changed.front().creator={1};
  Check(std::holds_alternative<CaptureCellFailure>(PlanFamilyCells(family,{},changed)));
  changed=requirements.demands;changed.front().demand.creation={UINT32_MAX};
  Check(std::holds_alternative<CaptureCellFailure>(PlanFamilyCells(family,{},changed)));
  // Requirements-only input is never accepted by the publication constructor.
  auto unfinished=Take<CreatorClasses>(DiscoverCreatorClasses(input));
  Take<CreatorClassCost>(PrepareCreatorClasses(input,unfinished,nullptr,{1},RecoveryMode::kCompleteOnly,{},ClassRootBinding::kRequirements));
  auto& body=std::get<FamilyPreparation>(unfinished.tasks.front().preparation->bodies.front());
  const FamilyDestination destination{Strictness::kStrict,nullptr,FunctionFragment::kClassMember,{1}};
  auto rejected=PreparedFamily::Finish(*file,header,index,lookup,std::move(body),destination,{},&owner.Normal(),owner.Storage());
  Check(std::get<FamilyPrepareError>(std::get<FamilyPrepareFailure>(rejected).cause)==FamilyPrepareError::kClassRequirements);
}
}
int main(int argc,char**argv){
  Check(argc==2);const std::filesystem::path root(argv[1]);
  for(auto contract:{SourceContract::kLanguageNumericConstructorsV1,SourceContract::kLanguageSourceV2}) {
    Run(root/"forward.abc",1,contract);
    for(auto n:{200,201,400,800}) {
      Run(root/("forwardwidth"+std::to_string(n)+".abc"),n,contract);
      Run(root/("forwarddepth"+std::to_string(n)+".abc"),1,contract);
    }
  }
}
