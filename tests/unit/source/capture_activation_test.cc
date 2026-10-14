#include "../../../src/source/capture_plan.h"
#include "../../support/analysis/ssa_fixture.h"
#include "../../../src/analysis/semantic_locals.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
struct Input { abc::MethodBody body; SemanticMethod method; SemanticProof proof; InstructionId creation,store; };
std::optional<Input> Make(unsigned blocks,bool copy,bool loop=false,bool reset=false){
 std::vector<std::uint8_t> code;
 const auto op=[&](abc::Opcode name){auto n=static_cast<unsigned>(name);code.push_back(n&255);if(n>255)code.push_back(n>>8);};
 // A stable-environment loop: each iteration pushes and later pops its own
 // environment. No fixed block layout is used by the production proof.
 if(loop){op(abc::Opcode::kLDA_V8);code.push_back(0);op(abc::Opcode::kJEQZ_IMM8);code.push_back(0);}
 op(abc::Opcode::kNEWLEXENV_IMM8);code.push_back(1);
 for(unsigned i=0;i<blocks;++i){op(abc::Opcode::kJMP_IMM8);code.push_back(2);}
 op(abc::Opcode::kLDTRUE);op(abc::Opcode::kJEQZ_IMM8);code.push_back(3);op(abc::Opcode::kDEBUGGER);
 if(copy){op(abc::Opcode::kLDA_V8);code.push_back(0);op(abc::Opcode::kSTA_V8);code.push_back(1);}
 if(reset){op(abc::Opcode::kLDHOLE);op(abc::Opcode::kSTLEXVAR_IMM4_IMM4);code.push_back(0);}
 op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);code.insert(code.end(),{0,0,0,0});
 op(abc::Opcode::kLDUNDEFINED);op(abc::Opcode::kSTLEXVAR_IMM4_IMM4);code.push_back(0);
 if(loop){op(abc::Opcode::kPOPLEXENV);auto jump=code.size();op(abc::Opcode::kJMP_IMM8);code.push_back(static_cast<std::uint8_t>(-static_cast<int>(jump)));code[3]=static_cast<std::uint8_t>(code.size()-2);}
 op(abc::Opcode::kRETURNUNDEFINED);
 auto body=test::Body(code);auto method=std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,3));
 auto verified=VerifySemanticSource(body,method,3,0,{}, {}, {},ClosureWrites::kLanguage);
 if(reset){test::Check(std::holds_alternative<SemanticProofFailure>(verified));return {};}
 auto proof=std::get<SemanticProof>(std::move(verified));InstructionId creation,store;
 for(std::size_t i=0;i<method.instructions.size();++i){if(std::holds_alternative<FunctionCreation>(method.instructions[i].operation))creation={static_cast<std::uint32_t>(i)};if(std::holds_alternative<LexicalStore>(method.instructions[i].operation))store={static_cast<std::uint32_t>(i)};}
 return Input{std::move(body),std::move(method),std::move(proof),creation,store};
}
void Run(unsigned count,bool copy,bool loop=false){
 auto input=*Make(loop?1:count,copy,loop);const auto& method=input.method;auto& proof=input.proof;
 const CreationBinding creation{method.instructions[input.creation.value].pc,std::get<FunctionCreation>(method.instructions[input.creation.value].operation),{{0},{}},OrdinaryCapture{EnvironmentId{0}}};
 std::vector<CaptureUse> uses(count,CaptureUse{{{0},{0}},CaptureDomain::kGuarded});
 const auto local=[&](std::size_t bytes,std::uint64_t work=SemanticBudget{}.work) {
  return ProveLocalAccesses(method,proof.environments,{bytes,work},ClosureWrites::kLanguage,{});
 };
 std::size_t lo=0,hi=SemanticBudget{}.bytes;
 test::Check(std::holds_alternative<LocalAccessPlan>(local(hi)));
 while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<LocalAccessPlan>(local(mid)))hi=mid;else lo=mid+1;}
 const auto local_work=std::get<LocalAccessPlan>(local(lo)).work_units;
 test::Check(lo && std::holds_alternative<SemanticProofFailure>(local(lo-1)));
 test::Check(std::holds_alternative<LocalAccessPlan>(local(lo,local_work)));
 test::Check(local_work && std::holds_alternative<SemanticProofFailure>(local(lo,local_work-1)));
 const ScopeFacts scopes(method,proof);
 const auto built=BuildCapturePlan(scopes,input.creation,creation,uses);
 test::Check(std::holds_alternative<CapturePlan>(built));
 const auto& plan=std::get<CapturePlan>(built);test::Check(plan.bindings.at(0).deferred==input.store);
 test::Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scopes,input.creation,creation,uses,plan)));
 SemanticBudget budget{plan.allocated_bytes,plan.work};
 test::Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes,input.creation,creation,uses,budget)));
 --budget.bytes;test::Check(std::get<CaptureFailure>(BuildCapturePlan(scopes,input.creation,creation,uses,budget))==CaptureError::kResourceLimit);
 ++budget.bytes;--budget.work;test::Check(std::get<CaptureFailure>(BuildCapturePlan(scopes,input.creation,creation,uses,budget))==CaptureError::kWorkLimit);
 auto forged=plan;forged.bindings[0].deferred=InstructionId{0};test::Check(std::get<CaptureFailure>(VerifyCapturePlan(scopes,input.creation,creation,uses,forged))==CaptureError::kBinding);
 auto locals=proof.locals;
 // A second direct store/reset invalidates the unique-store activation proof.
 locals.stores.push_back(locals.stores.front());
 ScopeFacts reset(method,proof.environments,locals,proof.defined_captures,ClosureWrites::kLanguage);
 test::Check(std::holds_alternative<CaptureFailure>(BuildCapturePlan(reset,input.creation,creation,uses)));
 locals=proof.locals;
 // Move allocation to a non-dominating sibling block in the original CFG.
 for(std::size_t b=0;b<method.flow.blocks.size();++b){const auto& block=method.flow.blocks[b];if(block.instruction_count==1 && std::holds_alternative<DebuggerOperation>(method.instructions[block.first_instruction].operation)){locals.allocations[0]=InstructionId{block.first_instruction};break;}}
 ScopeFacts wrong(method,proof.environments,locals,proof.defined_captures,ClosureWrites::kLanguage);
 test::Check(std::holds_alternative<CaptureFailure>(BuildCapturePlan(wrong,input.creation,creation,uses)));
 for(auto domain:{CaptureDomain::kLanguage,CaptureDomain::kDefined,CaptureDomain::kSourceAccess}){
  CaptureUse strict{{{0},{0}},domain};ScopeFacts premise(method,proof,{},true);
  test::Check(std::holds_alternative<CaptureFailure>(BuildCapturePlan(premise,input.creation,creation,{&strict,1})));
 }
 ScopeFacts unknown(method,proof.environments,proof.locals,proof.defined_captures,ClosureWrites::kUnknown);
 test::Check(std::holds_alternative<CaptureFailure>(BuildCapturePlan(unknown,input.creation,creation,uses)));
 std::cout<<"activation blocks="<<(loop?1:count)<<" uses="<<count<<" copy="<<copy<<" loop="<<loop<<" exact="<<plan.allocated_bytes<<'/'<<plan.work<<'\n';
}
}
int main(){for(unsigned n:{1U,200U,201U,400U,800U}){Run(n,false);Run(n,true);}Run(1,false,true);Make(1,false,false,true);}
