#include "abcd/analysis/property_delete.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/exception_ssa.h"
#include "../../../src/analysis/value_operands.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;using namespace analysis;using test::Check;
int main(){
 const auto body=test::Body({0xc2,2,0x64});
 auto instruction=body.instructions[0].decoded;
 Check(std::get<PropertyDelete>(LiftPropertyDelete(instruction,3)).object==abc::RegisterId{2});
 Check(std::get<ScalarError>(LiftPropertyDelete(instruction,2))==ScalarError::kRegisterRange);
 auto malformed=instruction;malformed.operands[0].kind=abc::OperandKind::kImmediate;
 Check(std::get<ScalarError>(LiftPropertyDelete(malformed,3))==ScalarError::kMalformedInstruction);
 const auto semantic=std::get<SemanticInstruction>(LiftSemanticInstruction(body.instructions[0],abc::ProfileId::kDynamic1206,3));
 const auto inputs=std::get<ValueOperands>(DescribeValueOperands(semantic));
 Check(inputs.count==2&&inputs.operands[0].role==OperandRole::kBase&&inputs.operands[1].role==OperandRole::kKey);
 Check(inputs.write&&std::holds_alternative<Accumulator>(*inputs.write));
 Check(semantic.effects.exceptional==ExceptionalCompletion::kMayThrow&&semantic.effects.before_throw==semantic.effects.normal);
 for(const auto n:{200U,201U,400U,800U}) {
  std::vector<std::uint8_t> bytes;
  for(unsigned i=0;i<n;++i)bytes.insert(bytes.end(),{0x60,1,0xc2,0}); // key input, delete object
  bytes.push_back(0x64);
  const auto input_body=test::Body(bytes);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
  const auto input=std::get<SemanticMethod>(BuildSemanticMethod(input_body,abc::ProfileId::kDynamic1206,frame.registers));
  const auto proof=std::get<SemanticProof>(VerifySemanticSource(input_body,input,frame.registers,frame.locals));
  const auto graph=std::get<ValueProgram>(ImportNormalValues(input_body,input,proof,frame));
  Check(!VerifyImportedValues(graph,input,proof,frame));
  for(unsigned i=0;i<n;++i){const auto& op=graph.ops[2*i+1];Check(std::holds_alternative<ValuePropertyDelete>(op.operation)&&op.result&&graph.values[op.result->value].domain.IsBoolean());}
  auto bad=graph;const auto at=bad.ops[1].first_use.value;std::swap(bad.uses[at].operand,bad.uses[at+1].operand);
  Check(VerifyImportedValues(bad,input,proof,frame).has_value());
  bad=graph;bad.ops[1].operation=ValuePropertyRead{};Check(VerifyImportedValues(bad,input,proof,frame).has_value());
  bad=graph;bad.ops[1].before_throw=LanguageEffect::kNone;Check(VerifyImportedValues(bad,input,proof,frame).has_value());
  bad=graph;bad.ops[1].result.reset();Check(VerifyImportedValues(bad,input,proof,frame).has_value());
  auto caught=input_body;const auto end=static_cast<std::uint32_t>(bytes.size());caught.instructions.push_back({{end},input_body.instructions.back().decoded,{}});
  abc::TryRegion region;region.length=end-1;region.handler_count=1;caught.tries.push_back(region);
  abc::CatchHandler handler;handler.offset={end};handler.length=1;caught.handlers.push_back(handler);
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(caught,abc::ProfileId::kDynamic1206,frame.registers));
  for(const auto& node:flow.nodes)if(std::holds_alternative<PropertyDelete>(flow.instructions[node.instruction.value].operation)&&node.phase==ExceptionPhase::kBefore)Check(node.exception.has_value());
 }
 // Register renaming and SSA copies preserve the independent base/key roles.
 for(const std::uint8_t object : {2U,3U,4U}) {
  const auto copied=test::Body({0x60,1,0x61,0,0x60,0,0xc2,object,0x64});
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,5,{}));
  const auto input=std::get<SemanticMethod>(BuildSemanticMethod(copied,abc::ProfileId::kDynamic1206,frame.registers));
  const auto proof=std::get<SemanticProof>(VerifySemanticSource(copied,input,frame.registers,frame.locals));
  const auto graph=std::get<ValueProgram>(ImportNormalValues(copied,input,proof,frame));
  Check(!VerifyImportedValues(graph,input,proof,frame));
 }
}
