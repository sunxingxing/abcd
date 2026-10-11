#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_tree.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "source/initialized_guards.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;using namespace abcd::analysis;using namespace abcd::source;
namespace {
template<class T,class V> T Take(V&& value,std::source_location at=std::source_location::current()) {
  if(!std::holds_alternative<T>(value)) {
    std::cerr<<"guard fixture variant "<<at.line()<<" alternative "<<value.index()<<'\n';
    std::visit([](const auto& error){if constexpr(std::is_enum_v<std::decay_t<decltype(error)>>)std::cerr<<static_cast<unsigned>(error)<<'\n';},value);
    std::abort();
  }
  return std::get<T>(std::forward<V>(value));
}
test::GuardCost Run(abc::ProfileId profile,unsigned count,bool split,bool relocated,bool emit) {
  const auto op=[](abc::Opcode code){return static_cast<std::uint8_t>(code);};
  const std::uint8_t saved=relocated?2:0,locals=saved+1;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kNEWLEXENV_IMM8),1,op(abc::Opcode::kLDA_V8),locals,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),0};
  const auto begin=static_cast<unsigned>(bytes.size());
  for(unsigned i=0;i<count;++i) {
    bytes.insert(bytes.end(),{op(abc::Opcode::kLDLEXVAR_IMM4_IMM4),0,op(abc::Opcode::kSTA_V8),saved,op(abc::Opcode::kLDA_V8),saved});
    if(split)bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),2});
    constexpr auto guard=static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    bytes.insert(bytes.end(),{static_cast<std::uint8_t>(guard),static_cast<std::uint8_t>(guard>>8),0,0});
  }
  bytes.insert(bytes.end(),{op(abc::Opcode::kLDA_V8),static_cast<std::uint8_t>(locals+1),op(abc::Opcode::kCALLARG0_IMM8),0,
      op(abc::Opcode::kLDA_V8),saved,op(abc::Opcode::kRETURN)});
  const auto end=static_cast<unsigned>(bytes.size());bytes.push_back(op(abc::Opcode::kRETURN));
  auto body=test::Body(bytes);abc::TryRegion region;region.offset={begin};region.length=end-begin;region.handler_count=1;
  body.tries.push_back(region);abc::CatchHandler handler;handler.offset={end};handler.length=1;body.handlers.push_back(handler);
  const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(locals,2,{}));
  const auto raw=Take<ExceptionFlow>(BuildExceptionFlow(body,profile,frame.registers));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(raw,frame.registers,quota));
  const auto env=Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(raw));
  const auto local=Take<ExceptionLocals>(ProveExceptionLocals(raw,env));std::vector<EntryValueFact> entries;
  for(unsigned i=0;i<ssa.Values().size();++i)if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))entries.push_back({{i},ValueKinds::Language()});
  IdfQuota work{kDefaultSemanticWork};const auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(
      raw,ssa,entries,work,{},PendingExceptionValues::kLanguage,local.loads,{},CallResultValues::kLanguage));
  const auto proof=Take<ExceptionInputProof>(VerifyExceptionInputs(raw,ssa,env,values));
  const PhasedValueInput input{raw,ssa,env,values,proof,frame};const auto graph=Take<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation=Take<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=Take<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.parameters.resize(2);syntax.identity.profile=profile;
  std::vector<ExpressionConstant> constants(graph.ops.size());const ExceptionTreeInput context{body,input,regions,control};
  const auto draft=Take<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{constants}));
  unsigned guards=0;for(const auto& operation:graph.ops)if(std::holds_alternative<ValueGuard>(operation.operation))++guards;
  test::GuardCheck(guards==count&&draft.flow&&draft.flow->original);
  const auto cost=test::CheckInitializedGuards(draft.tree,*draft.flow->original,guards);
  if(emit) {
    std::ostringstream output;Take<PrintedFunction>(PrintFunctionTree(draft.tree,output,{}, {},nullptr,draft.flow->original.get()));
    std::cout<<nlohmann::json{{"count",count},{"split",split},{"relocated",relocated},{"source",output.str()}}<<'\n';
  } else std::cout<<static_cast<unsigned>(profile)<<' '<<count<<' '<<split<<' '<<relocated<<' '<<cost.work<<' '<<cost.bytes<<' '<<cost.nodes<<' '<<cost.edges<<'\n';
  return cost;
}
}
int main(int argc,char**) {
  for(const auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(bool split:{false,true})for(bool relocated:{false,true})Run(profile,2,split,relocated,argc>1);
  std::optional<test::GuardCost> previous;
  for(unsigned count:{1,200,201,400,800}) {
    const auto cost=Run(abc::ProfileId::kDynamic1206,count,false,false,argc>1);
    // Work follows semantic graph size; vector capacity can step at 200/201.
    if(previous&&count>=201){const auto ratio=count==201?1.1:2.5;test::GuardCheck(cost.work<previous->work*ratio&&cost.bytes<previous->bytes*2.5&&cost.nodes<previous->nodes*ratio&&cost.edges<previous->edges*ratio);}
    previous=cost;
  }
}
