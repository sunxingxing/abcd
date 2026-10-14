#include "../../../src/source/exception_plan.h"
#include "../../../src/source/exception_control.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/exception_region_json.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include "abcd/abc/metadata_index.h"
#include "abcd/corpus/abc_file.h"
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
constexpr auto U=static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED);
constexpr auto R=static_cast<std::uint8_t>(abc::Opcode::kRETURN);
constexpr auto T=static_cast<std::uint8_t>(abc::Opcode::kLDTRUE);
constexpr auto J=static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8);
void Region(abc::MethodBody& b,unsigned start,unsigned size,unsigned handler,unsigned length) {
  b.tries.push_back({{start},size,static_cast<std::uint32_t>(b.handlers.size()),1});
  abc::CatchHandler h;h.offset={handler};h.length=length;b.handlers.push_back(h);
}
template<class F> void With(const abc::MethodBody& body,F check) {
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,1,{}));
  const auto built=BuildExceptionFlow(body,abc::ProfileId::kDynamic1301,frame.registers,{}, {},&frame);
  test::Check(std::holds_alternative<ExceptionFlow>(built));const auto& flow=std::get<ExceptionFlow>(built);
  IdfQuota quota{kDefaultSemanticWork};const auto renamed=ExceptionSsa::Build(flow,frame.registers,quota);
  const auto& ssa=std::get<ExceptionSsa>(renamed);std::vector<EntryValueFact> entries;
  for(std::uint32_t i=0;i<ssa.Values().size();++i)
    if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin)&&std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot))
      entries.push_back({{i},ValueKinds::Language()});
  const auto bound=AnalyzeExceptionEnvironments(flow);const auto& env=std::get<EnvironmentFlow>(bound);
  IdfQuota vquota{kDefaultSemanticWork};const auto propagated=ExceptionValueFlow::Build(flow,ssa,entries,vquota);
  const auto& values=std::get<ExceptionValueFlow>(propagated);
  const auto checked=VerifyExceptionInputs(flow,ssa,env,values);const auto& proof=std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};const auto imported=ImportPhasedValues(input);
  const auto& graph=std::get<ValueProgram>(imported);check(flow,graph,input);
}
void Inactive(unsigned count,bool tree) {
  std::vector<std::uint8_t> bytes;for(unsigned i=0;i<count;++i)bytes.insert(bytes.end(),{U,J,4,T,R});bytes.push_back(R);
  auto body=test::Body(bytes);for(unsigned i=0;i<count;++i)Region(body,i*5,1,i*5+3,2);
  With(body,[&](const auto&,const auto& graph,const auto& input){
    auto built=BuildExceptionPlan(body,graph,input);test::Check(std::holds_alternative<ExceptionPlan>(built));
    const auto& plan=std::get<ExceptionPlan>(built);test::Check(plan.layout.source_order.empty());
    for(const auto& region:plan.layout.regions)test::Check(region.inactive&&region.inactive->nonthrowing==1&&region.inactive->unreachable==0);
    auto forged=plan;forged.layout.regions[0].inactive.reset();test::Check(std::holds_alternative<ExceptionPlanError>(VerifyExceptionPlan(body,graph,input,forged)));
    forged=plan;++forged.layout.regions[0].inactive->dispatched_elsewhere;test::Check(std::holds_alternative<ExceptionPlanError>(VerifyExceptionPlan(body,graph,input,forged)));
    const auto control_result=BuildExceptionControl(body,graph,input,plan);test::Check(std::holds_alternative<ExceptionControl>(control_result));
    const auto& control=std::get<ExceptionControl>(control_result);for(const auto& r:control.regions)test::Check(!r);
    auto bad_control=control;bad_control.regions[0].emplace();test::Check(std::holds_alternative<ExceptionControlError>(VerifyExceptionControl(body,graph,input,plan,bad_control)));
    const auto work=std::get<std::uint64_t>(VerifyExceptionPlan(body,graph,input,plan));
    test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionPlan(body,graph,input,plan,{SemanticBudget{}.bytes,work})));
    test::Check(std::get<ExceptionPlanError>(VerifyExceptionPlan(body,graph,input,plan,{SemanticBudget{}.bytes,work-1}))==ExceptionPlanError::kWorkLimit);
    std::size_t low=0,high=SemanticBudget{}.bytes;
    while(low<high){const auto mid=low+(high-low)/2;const auto v=BuildExceptionPlan(body,graph,input,{mid});if(std::holds_alternative<ExceptionPlan>(v))high=mid;else {test::Check(std::get<ExceptionPlanError>(v)==ExceptionPlanError::kResourceLimit);low=mid+1;}}
    test::Check(std::holds_alternative<ExceptionPlan>(BuildExceptionPlan(body,graph,input,{low})));
    test::Check(std::get<ExceptionPlanError>(BuildExceptionPlan(body,graph,input,{low-1}))==ExceptionPlanError::kResourceLimit);
    std::cout<<count<<" regions bytes="<<low<<" work="<<work<<'\n';
    if(!tree)return;
    const auto evaluation=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.parameters.resize(1);
    const std::vector<ExpressionConstant> constants(graph.ops.size());const ExceptionTreeInput context{body,input,plan,control};
    const auto drafted=BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{constants});test::Check(std::holds_alternative<ExceptionFunctionDraft>(drafted));
    const auto& draft=std::get<ExceptionFunctionDraft>(drafted);const auto semantics=VerifyExceptionTreeSemantics(graph,context,evaluation,draft.tree,{constants});
    test::Check(std::holds_alternative<TreeSemantics>(semantics));
    // Retained original evidence is independently checked again by recovery.
    const auto verified=VerifyExceptionTreeFlow(graph,context,evaluation,draft.tree);
    test::Check(std::holds_alternative<TreeFlow>(verified));auto flow=std::get<TreeFlow>(verified);TreeCheckBudget budget({});
    auto original=RetainOriginalFlow(graph,draft.tree,flow,budget);VerifyInactiveOriginalRegions(*original,budget);
    const auto rejects=[&]{TreeCheckBudget fresh({});bool refused=false;try{VerifyInactiveOriginalRegions(*original,fresh);}catch(TreeStructureError e){refused=e==TreeStructureError::kOwnership;}test::Check(refused);};
    ++original->inactive_regions[0].nonthrowing;rejects();--original->inactive_regions[0].nonthrowing;
    original->inactive_regions.clear();rejects();
  });
}
void CopiesAndSplit() {
  for(bool split:{false,true}) {
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(abc::Opcode::kLDA_V8),0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8),0};
    if(split)bytes.insert(bytes.end(),{J,2});
    bytes.insert(bytes.end(),{static_cast<std::uint8_t>(abc::Opcode::kLDA_V8),0,R});
    const auto handler=static_cast<unsigned>(bytes.size());
    bytes.insert(bytes.end(),{static_cast<std::uint8_t>(abc::Opcode::kLDA_V8),0,R});
    auto body=test::Body(bytes);Region(body,0,handler,handler,3);
    With(body,[&](const auto&,const auto& graph,const auto& input){
      const auto plan=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
      test::Check(plan.layout.regions[0].inactive&&plan.layout.regions[0].inactive->nonthrowing==(split?5U:4U));
      test::Check(std::holds_alternative<ExceptionControl>(BuildExceptionControl(body,graph,input,plan)));
    });
  }
}
void ActiveAndShadow() {
  auto body=test::Body(std::vector<std::uint8_t>{U,0xfe,0,R,T,R});Region(body,0,3,3,1);Region(body,1,2,4,2);
  With(body,[&](const auto&,const auto& graph,const auto& input){
    const auto plan=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));test::Check(!plan.layout.regions[0].inactive&&plan.regions[0].binding);
    test::Check(plan.layout.regions[1].inactive&&plan.layout.regions[1].inactive->dispatched_elsewhere==1);
    const auto control=BuildExceptionControl(body,graph,input,plan);test::Check(std::holds_alternative<ExceptionControl>(control));
    auto forged=plan;forged.layout.regions[0].inactive=InactiveExceptionRegion{{0},plan.layout.regions[0].body,plan.layout.regions[0].handler,1,1,0};
    test::Check(std::holds_alternative<ExceptionPlanError>(VerifyExceptionPlan(body,graph,input,forged)));
  });
  body=test::Body(std::vector<std::uint8_t>{U,R});Region(body,0,1,1,1);
  With(body,[&](const auto&,const auto& graph,const auto& input){const auto plan=BuildExceptionPlan(body,graph,input);test::Check(std::holds_alternative<ExceptionPlanFailure>(plan));const auto& f=std::get<ExceptionPlanFailure>(plan);test::Check(f.cause.kind==CatchRegionError::kNormalEntry&&f.exception_region&&f.exception_region->body_pc==abc::CodeOffset{0}&&f.exception_region->handler_pc==abc::CodeOffset{1});});
}
}
void Fixture(const char* path) {
  const auto file=std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto hash=std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  unsigned inactive=0,active=0,unknown_extent=0;
  for(const auto& method:index.methods) {
    if(!method.code)continue;
    const auto body=std::get<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method.code));
    if(body.tries.empty())continue;
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers,method.code->arguments,{}));
    const auto built_flow=BuildExceptionFlow(body,header.profile,frame.registers,{}, {},&frame);
    if(!std::holds_alternative<ExceptionFlow>(built_flow))std::cerr<<"fixture flow "<<method.name<<" variant "<<built_flow.index()<<std::endl;
    const auto flow=std::get<ExceptionFlow>(built_flow);
    const auto built_regions=BuildExceptionRegions(body,flow);
    if(const auto* f=std::get_if<CatchRegionFailure>(&built_regions)) {
      test::Check(f->kind==CatchRegionError::kHandlerShape&&f->region);
      test::Check(body.handlers[body.tries[f->region->value].first_handler].length==0);
      ++unknown_extent;continue;
    }
    const auto regions=std::get<ExceptionRegions>(built_regions);
    for(const auto& r:regions.regions) {
      if(r.inactive){++inactive;test::Check(r.inactive->dispatched_elsewhere==0&&r.inactive->nonthrowing>0);}
      else ++active;
    }
  }
  test::Check((inactive==2&&active==1&&unknown_extent==0)||(inactive==0&&active==0&&unknown_extent==3));
}
int main(int argc,char**argv){if(argc>1){for(int i=1;i<argc;++i)Fixture(argv[i]);return 0;}ActiveAndShadow();CopiesAndSplit();Inactive(1,true);for(unsigned n:{200,201,400,800})Inactive(n,false);
  ExceptionRegionEvidence e{{0},abc::CodeOffset{0},{},{12},{15}};
  test::Check(ParseExceptionRegionJson(ExceptionRegionJson(e))==e);
}
