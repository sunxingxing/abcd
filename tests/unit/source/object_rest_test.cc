#include "../../../src/source/object_rest_plan.h"
#include "abcd/analysis/object_rest.h"
#include "abcd/analysis/exception_edge.h"
#include "abcd/analysis/value_import.h"
#include "../../../src/analysis/operand_view.h"
#include "../../../src/analysis/value_flow.h"
#include <cstdlib>
#include <iostream>
using namespace abcd;using namespace abcd::analysis;using namespace abcd::source;
namespace {
void Check(bool x){if(!x)std::abort();}
struct Fixture{ValueProgram g;EvaluationPlan p;};
Fixture Make(std::uint32_t count){
 Fixture f;auto& g=f.g;g.values.push_back({EntryDefinition{ParameterEntry{0}},ValueKinds::Language(),{}});
 auto add=[&](ValueOperation operation,std::uint32_t block,std::vector<std::pair<OperandRole,ValueOperand>> inputs,bool result,LanguageEffect effects=LanguageEffect::kNone,ExceptionalCompletion exceptional=ExceptionalCompletion::kImpossible){OpId id{static_cast<std::uint32_t>(g.ops.size())};UseId first{static_cast<std::uint32_t>(g.uses.size())};std::optional<ValueId> value;if(result){value=ValueId{static_cast<std::uint32_t>(g.values.size())};g.values.push_back({OpResult{id,{}},std::holds_alternative<ValueString>(operation)?ValueKinds::String():ValueKinds::Language(),{}});}for(const auto& [role,input]:inputs){UseId use{static_cast<std::uint32_t>(g.uses.size())};g.uses.push_back({id,role,ReadPurpose::kOperand,input});if(const auto* v=std::get_if<ValueId>(&input)){auto old=g.values[v->value].first_use;g.uses.back().next=old;if(old)g.uses[old->value].previous=use;g.values[v->value].first_use=use;}}const auto order=g.blocks[block].op_count++;g.origins.push_back({{id.value},{id.value},{}});g.ops.push_back({operation,{block},order,first,static_cast<std::uint32_t>(inputs.size()),value,effects,effects,exceptional,{id.value}});if(exceptional!=ExceptionalCompletion::kImpossible)g.exceptional_exits.push_back({id,{id.value},effects,{}});return value;};
 g.blocks.resize(4);g.blocks[0].first_op={0};auto n=add(ValueBinary{BinaryOperator::kStrictNotEqual},0,{{OperandRole::kLeft,ValueId{0}},{OperandRole::kRight,SpecialValue::kNull}},true);add(ValueBranch{UnaryOperator::kToBoolean},0,{{OperandRole::kCondition,*n}},false);
 g.blocks[1].first_op={static_cast<std::uint32_t>(g.ops.size())};auto u=add(ValueBinary{BinaryOperator::kStrictNotEqual},1,{{OperandRole::kLeft,ValueId{0}},{OperandRole::kRight,SpecialValue::kUndefined}},true);add(ValueBranch{UnaryOperator::kToBoolean},1,{{OperandRole::kCondition,*u}},false);
 g.blocks[2].first_op={static_cast<std::uint32_t>(g.ops.size())};std::vector<std::pair<OperandRole,ValueOperand>> keys{{OperandRole::kBase,ValueId{0}}};for(std::uint32_t i=0;i<count;++i){auto key=add(ValueString{abc::PoolIndex{i}},2,{},true);keys.push_back({OperandRole::kArgument,*key});add(ValueNamedRead{abc::PoolIndex{i}},2,{{OperandRole::kBase,ValueId{0}}},true,LanguageEffect::kHeapRead|LanguageEffect::kUserCode,ExceptionalCompletion::kMayThrow);}
 if(!count)keys.push_back({OperandRole::kArgument,SpecialValue::kUndefined});auto rest=add(ValueObjectRest{count?count:1},2,keys,true,LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite|LanguageEffect::kAllocation|LanguageEffect::kUserCode,ExceptionalCompletion::kMayThrow);add(ValueReturn{},2,{{OperandRole::kReturnValue,*rest}},false);
 g.blocks[3].first_op={static_cast<std::uint32_t>(g.ops.size())};add(ValueThrow{ThrowKind::kPatternNonCoercible},3,{},false,LanguageEffect::kAllocation,ExceptionalCompletion::kAlwaysThrow);
 g.edges={{{0},{1},FlowEdgeKind::kTaken,0},{{0},{3},FlowEdgeKind::kFallthrough,1},{{1},{2},FlowEdgeKind::kTaken,0},{{1},{3},FlowEdgeKind::kFallthrough,1}};g.blocks[0].first_edge={0};g.blocks[0].edge_count=2;g.blocks[1].first_edge={2};g.blocks[1].edge_count=2;g.blocks[2].first_edge={4};g.blocks[3].first_edge={4};
 f.p.operations.resize(g.ops.size());f.p.aliases.resize(g.values.size());for(std::uint32_t i=0;i<g.values.size();++i)f.p.aliases[i]={i};return f;
}
Fixture Phased(Fixture f) {
 auto old=f.g.blocks;auto arcs=f.g.edges;auto& g=f.g;g.blocks.clear();g.edges.clear();g.phases.clear();
 for(std::uint32_t i=0;i<g.ops.size();++i){auto& op=g.ops[i];const auto old_block=op.block.value;const auto terminal=op.order+1==old[old_block].op_count;
  op.block={2*i};op.order=0;
  if(op.result)std::get<OpResult>(g.values[op.result->value].origin).commit=WorkBlockId{2*i+1};
  const auto before_edge=static_cast<std::uint32_t>(g.edges.size());
  const bool normal=op.exceptional!=ExceptionalCompletion::kAlwaysThrow;
  if(normal)g.edges.push_back({{2*i},{2*i+1},FlowEdgeKind::kFallthrough,0});
  g.blocks.push_back({{i},1,{before_edge},static_cast<std::uint8_t>(normal)});
  g.phases.push_back({{2*i},{i},ExceptionPhase::kBefore,OpId{i}});
  const auto first=static_cast<std::uint32_t>(g.edges.size());
  if(normal){if(terminal){for(std::uint32_t a=0;a<old[old_block].edge_count;++a){const auto& edge=arcs[old[old_block].first_edge.value+a];g.edges.push_back({{2*i+1},{2*old[edge.target.value].first_op.value},edge.kind,edge.successor});}}
   else g.edges.push_back({{2*i+1},{2*(i+1)},FlowEdgeKind::kFallthrough,0});}
  g.blocks.push_back({{i+1},0,{first},static_cast<std::uint8_t>(g.edges.size()-first)});
  g.phases.push_back({{2*i+1},{i},ExceptionPhase::kCommit,OpId{i},normal?PhaseReachability::kReachable:PhaseReachability::kUnreachable});
 }
 return f;
}
bool Proves(const Fixture& f,SemanticBudget grant={}){try{TreeCheckBudget b(grant);return BuildObjectRests(f.g,f.p,b).entries.size()==1;}catch(TreeStructureError){return false;}}
}
int main(){
 {SemanticInstruction intrinsic{};intrinsic.operation=ThrowOperation{ScalarValue{SpecialValue::kUndefined},ThrowKind::kPatternNonCoercible};intrinsic.effects.exceptional=ExceptionalCompletion::kAlwaysThrow;auto edge=ResolveExceptionEdge(intrinsic,{});Check(edge&&std::holds_alternative<PendingException>(edge->exception));intrinsic.operation=ThrowOperation{ScalarValue{SpecialValue::kUndefined}};edge=ResolveExceptionEdge(intrinsic,{});Check(edge&&std::holds_alternative<ThrownValue>(edge->exception));}

 for(bool wide:{false,true})for(auto count:{1U,200U,201U,400U,800U}) {
  if(!wide&&count>256)continue;
  const auto code=wide?abc::Opcode::kWIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8:abc::Opcode::kCREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8;
  const auto raw=static_cast<std::uint32_t>(code);std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(raw)};if(wide)bytes.push_back(raw>>8);bytes.push_back((count-1)&255);if(wide)bytes.push_back((count-1)>>8);bytes.push_back(1);bytes.push_back(3);
  auto decoded=std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes,abc::ProfileId::kDynamic2400));
  const auto semantic=std::get<SemanticInstruction>(LiftSemanticInstruction({{0},decoded},abc::ProfileId::kDynamic2400,count+4));
  const auto& op=std::get<ObjectRest>(semantic.operation);Check(op.count==count&&op.first.value==3&&op.object.value==1);
  auto operands=std::get<OperandView>(OperandView::Build(semantic,{sizeof(OperandView),1}));Check(operands.Count()==count+1);Check(operands.At(0)->role==OperandRole::kBase);Check(std::get<abc::RegisterId>(operands.At(count)->value).value==count+2);Check(!operands.At(count+1));
  Check(std::get<OperandViewError>(OperandView::Build(semantic,{sizeof(OperandView)-1,1}))==OperandViewError::kResourceLimit);Check(std::get<OperandViewError>(OperandView::Build(semantic,{sizeof(OperandView),0}))==OperandViewError::kWorkLimit);
  Check(std::holds_alternative<ScalarError>(LiftObjectRest(decoded,count+2)));auto wrong=decoded;wrong.operands[2].raw_bits=256;Check(std::get<ScalarError>(LiftObjectRest(wrong,2000))==ScalarError::kMalformedInstruction);
 }

 for(auto count:{0U,1U,200U,201U,400U,800U}){auto f=Make(count);auto split=Phased(f);if(!Proves(split)){auto flow=VerifyPhasedValueFlow(split.g);std::cerr<<"phased failed count="<<count<<" availability="<<(std::holds_alternative<ValueAvailabilityError>(flow)?int(std::get<ValueAvailabilityError>(flow)):-1)<<"\n";}Check(Proves(split));TreeCheckBudget budget({});auto proof=BuildObjectRests(f.g,f.p,budget);Check(proof.entries.size()==1&&proof.entries[0].fields.size()==count);TreeCheckBudget verify({});Check(VerifyObjectRests(f.g,f.p,proof,verify));auto omitted=proof; if(count){omitted.entries[0].fields.pop_back();TreeCheckBudget missing({});Check(!VerifyObjectRests(f.g,f.p,omitted,missing));} if(count>1){auto swapped=proof;std::swap(swapped.entries[0].fields[0],swapped.entries[0].fields[1]);TreeCheckBudget order({});Check(!VerifyObjectRests(f.g,f.p,swapped,order));} auto duplicated=proof;duplicated.entries.push_back(duplicated.entries.front());TreeCheckBudget repeated({});Check(!VerifyObjectRests(f.g,f.p,duplicated,repeated));auto forged=std::move(proof);++forged.entries[0].source.value;TreeCheckBudget check({});Check(!VerifyObjectRests(f.g,f.p,forged,check));
 auto wrong=f;wrong.g.edges[0].target={2};Check(!Proves(wrong));wrong=f;std::get<ValueBinary>(wrong.g.ops[0].operation).op=BinaryOperator::kStrictEqual;Check(!Proves(wrong));
 wrong=f;wrong.g.exceptional_exits.clear();Check(!Proves(wrong));
 if(count){wrong=f;wrong.g.exceptional_exits.front().handler=ValueHandler{{{0},{0},{123}},{3}};Check(!Proves(wrong));wrong=f;for(auto& op:wrong.g.ops)if(auto* read=std::get_if<ValueNamedRead>(&op.operation)){++read->name.value;break;}Check(!Proves(wrong));}
 for(bool bytes:{false,true}){std::uint64_t lo=0,hi=10000000;while(lo<hi){auto mid=(lo+hi)/2;SemanticBudget grant{};if(bytes)grant.bytes=mid;else grant.work=mid;if(Proves(f,grant))hi=mid;else lo=mid+1;}SemanticBudget exact{};if(bytes)exact.bytes=lo;else exact.work=lo;Check(Proves(f,exact));if(count==800)std::cout<<"800 "<<(bytes?"bytes=":"work=")<<lo<<"\n";if(lo){if(bytes)--exact.bytes;else --exact.work;Check(!Proves(f,exact));}}
 }
 std::cout<<"object-rest identity, guard, key, mutation and exact budgets passed\n";
}
