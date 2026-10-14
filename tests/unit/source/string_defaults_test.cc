#include "../../../src/source/default_body.h"
#include "../../../src/analysis/value_flow.h"
#include "../../../src/source/default_signature.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/function_abi.h"

#include <cstdio>
#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool ok, int line) { if (!ok) { std::fprintf(stderr,"string defaults:%d\n",line); std::abort(); } }
#define CHECK(x) Check((x), __LINE__)
// Independent SSA generator: alternating pool strings/numbers, with every
// initialized formal live in the body. Each join is the next parameter test.
ValueProgram Chain(std::uint32_t count) {
  ValueProgram g;
  for (std::uint32_t i=0;i<count;++i) {
    g.values.push_back({EntryDefinition{ParameterEntry{i}},ValueKinds::Language()});
    g.values.push_back({OpResult{{3*i}},ValueKinds::Language()});
    g.values.push_back({PhiDefinition{{2*i+2}},ValueKinds::Language()});
    g.values.push_back({OpResult{{3*i+2}},i%2 ? ValueKinds::Language() : ValueKinds::String()});
  }
  auto operation=[&](ValueOperation value,WorkBlockId block,std::uint32_t order,
                     std::optional<ValueId> result,std::initializer_list<std::pair<OperandRole,ValueOperand>> inputs) {
    const auto id=static_cast<std::uint32_t>(g.ops.size());
    const UseId first{static_cast<std::uint32_t>(g.uses.size())};
    for(const auto& [role,operand]:inputs) g.uses.push_back({OpId{id},role,ReadPurpose::kOperand,operand});
    g.ops.push_back({value,block,order,first,static_cast<std::uint32_t>(inputs.size()),result,
        LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,{id}});
    g.origins.push_back({{id},{id},{}});
  };
  for(std::uint32_t i=0;i<count;++i) {
    operation(ValueBinary{BinaryOperator::kStrictNotEqual},{2*i},0,ValueId{4*i+1},
        {{OperandRole::kLeft,ValueId{4*i}},{OperandRole::kRight,SpecialValue::kUndefined}});
    operation(ValueBranch{UnaryOperator::kTrueOrNonzeroNumber},{2*i},1,{},{{OperandRole::kCondition,ValueId{4*i+1}}});
    if(i%2) operation(ValueCopy{},{2*i+1},0,ValueId{4*i+3},{{OperandRole::kInput,analysis::IntegerLiteral{65280}}});
    else operation(ValueString{{i}},{2*i+1},0,ValueId{4*i+3},{});
    g.blocks.push_back({{3*i},2,{3*i},2});
    g.blocks.push_back({{3*i+2},1,{3*i+2},1});
    g.edges.push_back({{2*i},{2*i+2},FlowEdgeKind::kTaken,0});
    g.edges.push_back({{2*i},{2*i+1},FlowEdgeKind::kFallthrough,1});
    g.edges.push_back({{2*i+1},{2*i+2},FlowEdgeKind::kFallthrough,0});
    g.uses.push_back({ValueId{4*i+2},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{4*i},IncomingKey{EdgeId{3*i}}});
    g.uses.push_back({ValueId{4*i+2},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{4*i+3},IncomingKey{EdgeId{3*i+2}}});
  }
  g.blocks.push_back({{3*count},count+1,{3*count},0});
  for(std::uint32_t i=0;i<count;++i) {
    const ValueId result{static_cast<std::uint32_t>(g.values.size())};
    g.values.push_back({OpResult{{3*count+i}},ValueKinds::Language()});
    operation(ValueBinary{BinaryOperator::kStrictEqual},{2*count},i,result,
        {{OperandRole::kLeft,ValueId{4*i+2}},
         {OperandRole::kRight,i ? ValueOperand{ValueId{result.value-1}} : ValueOperand{SpecialValue::kUndefined}}});
  }
  operation(ValueReturn{},{2*count},count,{},{{OperandRole::kReturnValue,ValueId{static_cast<std::uint32_t>(g.values.size()-1)}}});
  std::vector<std::optional<UseId>> last(g.values.size());
  for(std::uint32_t i=0;i<g.uses.size();++i) {
    auto& use=g.uses[i];const auto* v=std::get_if<ValueId>(&use.operand);if(!v)continue;
    use.previous=last[v->value];if(use.previous)g.uses[use.previous->value].next=UseId{i};else g.values[v->value].first_use=UseId{i};
    last[v->value]=UseId{i};
  }
  CHECK(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(g)));
  return g;
}
template<class Success,class Error,class Run>
void ExactBytes(Run run,Error expected) {
  std::size_t lo=0,hi=64*1024*1024;
  while(lo<hi) {const auto mid=lo+(hi-lo)/2; if(std::holds_alternative<Success>(run(mid)))hi=mid;else lo=mid+1;}
  CHECK(lo>0);CHECK(std::holds_alternative<Success>(run(lo)));
  CHECK(std::get<Error>(run(lo-1))==expected);
}
void Signature() {
  const auto original=Chain(2);
  const auto parameters=std::get<ParameterPlan>(BuildParameterPlan(original,2));
  const auto candidates=std::get<DefaultCandidates>(FindDefaultCandidates(original,parameters,0));
  const auto body=std::get<DefaultBody>(DefaultBody::Build(original,parameters,candidates,0));
  const auto& graph=body.Graph();
  const auto control=std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation=std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  std::vector<ExpressionConstant> constants(graph.ops.size()); constants[2]=TextId{0};
  const ExpressionConstants values{constants,0,2,{}};
  FunctionSyntax syntax{{},{2},{0},CallableKind::kMethod,Strictness::kStrict,
      {{{},NameEvidence::kSynthesized},{{},NameEvidence::kSynthesized}},{},{}};
  syntax.texts={u"first",u"wrong"};
  auto built=BuildFunctionTree(graph,control,evaluation,std::move(syntax),values);
  if (const auto* e=std::get_if<FunctionTreeFailure>(&built)) std::fprintf(stderr,"tree kind=%u cause=%zu\n",unsigned(e->kind),e->cause.index());
  CHECK(std::holds_alternative<ConstructedFunction>(built));
  auto tree=std::move(std::get<ConstructedFunction>(built).tree);
  const DefaultSignatureContext context{control,evaluation,&values};
  CHECK(std::holds_alternative<DefaultSignatureProof>(BuildDefaultSignature(body,tree,{},&context)));
  CHECK(std::holds_alternative<DefaultSignatureProof>(VerifyDefaultSignature(body,tree,{},&context)));
  abc::FrameLayout frame{};frame.parameters=2;frame.registers=2;
  CHECK(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(graph,tree,frame,0)));
  CHECK(std::get<FunctionAbiError>(BuildFunctionAbi(graph,tree,frame,2))==FunctionAbiError::kLength);
  auto wrong=tree;
  std::swap(wrong.parameters[0].initializer,wrong.parameters[1].initializer);
  CHECK(std::holds_alternative<DefaultSignatureError>(VerifyDefaultSignature(body,wrong,{},&context)));
  wrong=tree;
  wrong.expressions[wrong.parameters[0].initializer->value].expression=TreeLiteral{TextId{1}};
  CHECK(std::get<DefaultSignatureError>(VerifyDefaultSignature(body,wrong,{},&context))==DefaultSignatureError::kLiteral);
  wrong=tree;wrong.parameters[0].initializer.reset();
  CHECK(std::holds_alternative<DefaultSignatureError>(VerifyDefaultSignature(body,wrong,{},&context)));
  CHECK(std::get<FunctionAbiError>(BuildFunctionAbi(graph,wrong,frame,0))==FunctionAbiError::kLength);
}
void Run(std::uint32_t count) {
  auto g=Chain(count);auto parameters=std::get<ParameterPlan>(BuildParameterPlan(g,count));
  auto candidates=std::get<DefaultCandidates>(FindDefaultCandidates(g,parameters,0));
  CHECK(candidates.values.size()==count);
  for(std::uint32_t i=0;i<count;++i) {
    CHECK(candidates.values[i].parameter==i);
    CHECK(candidates.values[i].text.has_value()==(i%2==0));
    if(i%2==0)CHECK(candidates.values[i].text->value==i);
  }
  auto proof=DefaultValueProof::Build(g,parameters,candidates,0);
  CHECK(std::holds_alternative<DefaultValueProof>(proof));
  auto body=DefaultBody::Build(g,parameters,candidates,0);
  CHECK(std::holds_alternative<DefaultBody>(body));
  CHECK(std::get<DefaultBody>(body).Strings().size()==(count+1)/2);
  CHECK(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(std::get<DefaultBody>(body).Graph())));
  auto bad=candidates;bad.values[0].text=abc::PoolIndex{9999};
  CHECK(std::get<DefaultProofError>(DefaultValueProof::Build(g,parameters,bad,0))==DefaultProofError::kLiteral);
  bad=candidates;std::swap(bad.values[0],bad.values[1]);
  CHECK(std::holds_alternative<DefaultProofError>(DefaultValueProof::Build(g,parameters,bad,0)));
  bad=candidates;bad.values[0].parameter=1;
  CHECK(std::get<DefaultProofError>(DefaultValueProof::Build(g,parameters,bad,0))==DefaultProofError::kParameter);
  bad=candidates;bad.values[1].text=abc::PoolIndex{0};
  CHECK(std::get<DefaultProofError>(DefaultValueProof::Build(g,parameters,bad,0))==DefaultProofError::kLiteral);
  bad=candidates;bad.values[0].text.reset();
  CHECK(std::get<DefaultProofError>(DefaultValueProof::Build(g,parameters,bad,0))==DefaultProofError::kLiteral);
  bad=candidates;bad.values[0].initializer=bad.values[1].initializer;
  CHECK(std::holds_alternative<DefaultProofError>(DefaultValueProof::Build(g,parameters,bad,0)));
  // A stale pool identity cannot survive independent rechecking of original IR.
  std::get<ValueString>(g.ops[2].operation).string={9999};
  CHECK(std::get<DefaultProofError>(DefaultValueProof::Build(g,parameters,candidates,0))==DefaultProofError::kLiteral);
  std::get<ValueString>(g.ops[2].operation).string={0};
  CHECK(candidates.work <= 30ULL*count+8);
  const auto work=std::get<DefaultValueProof>(proof).Work();
  CHECK(work <= 40ULL*count+8);
  CHECK(std::holds_alternative<DefaultCandidates>(FindDefaultCandidates(g,parameters,0,{64*1024*1024,candidates.work})));
  CHECK(std::get<DefaultCandidateError>(FindDefaultCandidates(g,parameters,0,{64*1024*1024,candidates.work-1}))==DefaultCandidateError::kWorkLimit);
  CHECK(std::holds_alternative<DefaultValueProof>(DefaultValueProof::Build(g,parameters,candidates,0,{64*1024*1024,work})));
  CHECK(std::get<DefaultProofError>(DefaultValueProof::Build(g,parameters,candidates,0,{64*1024*1024,work-1}))==DefaultProofError::kWorkLimit);
  ExactBytes<DefaultCandidates>([&](std::size_t bytes){return FindDefaultCandidates(g,parameters,0,{bytes,100000000});},DefaultCandidateError::kResourceLimit);
  ExactBytes<DefaultValueProof>([&](std::size_t bytes){return DefaultValueProof::Build(g,parameters,candidates,0,{bytes,100000000});},DefaultProofError::kResourceLimit);
  ExactBytes<DefaultBody>([&](std::size_t bytes){return DefaultBody::Build(g,parameters,candidates,0,{bytes,100000000});},DefaultBodyError::kResourceLimit);
  std::printf("defaults=%u discovery_work=%llu proof_work=%llu\n",count,
      static_cast<unsigned long long>(candidates.work),static_cast<unsigned long long>(work));
}
}
int main(){Signature();for(auto n:{2U,200U,201U,400U,800U})Run(n);}
