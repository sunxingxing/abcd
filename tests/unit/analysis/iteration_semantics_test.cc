#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_operands.h"
#include "../../../src/analysis/register_value_rule.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <array>
using namespace abcd;
using namespace abcd::analysis;
using test::Check;
namespace {
constexpr auto kProfile = abc::ProfileId::kDynamic1206;
void PhasedCommit() {
  // The acquisition can succeed with a primitive. Checking that result is a
  // separate instruction, with a separate exceptional edge and no acc write.
  auto body = test::Body({0x60,1,0x67,0,0x61,0,0xfe,0x05,0,0x64,
                          0x61,2,0x60,2,0xfe,0});
  abc::TryRegion region; region.offset={2}; region.length=7; region.handler_count=1;
  body.tries.push_back(region);
  abc::CatchHandler handler; handler.offset={10}; handler.length=6;
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,3,{}, {},&frame));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow,3,quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t i=0;i<ssa.Values().size();++i)
    if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
      entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,quota,{},PendingExceptionValues::kLanguage,{}, {},CallResultValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph,input)));
  unsigned acquisitions=0, checks=0;
  for (const auto& op : graph.ops) {
    const bool acquire=std::holds_alternative<ValueIteratorAcquire>(op.operation);
    const bool check=std::holds_alternative<ValueObjectCheck>(op.operation);
    if (!acquire && !check) continue;
    const OpId id{static_cast<std::uint32_t>(&op-graph.ops.data())};
    const auto edge=std::find_if(graph.exceptional_exits.begin(),graph.exceptional_exits.end(),
        [&](const auto& e){return e.op==id;});
    Check(edge!=graph.exceptional_exits.end() && edge->handler.has_value());
    if (acquire) {
      ++acquisitions; Check(op.result.has_value());
      const auto& origin=std::get<OpResult>(graph.values[op.result->value].origin);
      Check(origin.commit && graph.phases[origin.commit->value].phase==ExceptionPhase::kCommit);
      Check(graph.values[op.result->value].domain==ValueKinds::Language());
      auto bad=graph;
      std::get<OpResult>(bad.values[op.result->value].origin).commit=op.block;
      Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(bad,input)));
    } else { ++checks; Check(!op.result); }
    auto bad=graph; bad.ops[id.value].exceptional=ExceptionalCompletion::kImpossible;
    Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(bad,input)));
    bad=graph; bad.exceptional_exits.erase(bad.exceptional_exits.begin()+(edge-graph.exceptional_exits.begin()));
    Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(bad,input)));
  }
  Check(acquisitions==1 && checks==1);
}
void CopiesAndBlocks() {
  for (const std::uint8_t slot : {0,2}) {
    const auto body=test::Body({0x60,1,0xab,0,0,0x61,slot,0x4d,2,
                                0x60,slot,0x61,3,0xfe,0x05,3,0x64});
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,4,{}));
    const auto method=std::get<SemanticMethod>(BuildSemanticMethod(body,kProfile,4));
    const auto proof=std::get<SemanticProof>(VerifySemanticSource(body,method,4,0,{},
        CapturedValues::kReject,{},ClosureWrites::kUnknown,{},CallResultValues::kLanguage));
    const auto graph=std::get<ValueProgram>(ImportNormalValues(body,method,proof,frame));
    Check(!VerifyImportedValues(graph,method,proof,frame));
    Check(graph.blocks.size()>1);
    auto bad=graph;
    const auto at=std::find_if(bad.ops.begin(),bad.ops.end(),[](const auto& op) {
      return std::holds_alternative<ValueObjectCheck>(op.operation);
    });
    Check(at!=bad.ops.end());
    at->operation=ValueNop{};
    Check(VerifyImportedValues(bad,method,proof,frame).has_value());
  }
}
void ScaleAndBudget() {
  for (const unsigned n : {200U,201U,400U,800U}) {
    std::vector<std::uint8_t> bytes;
    for (unsigned i=0;i<n;++i) bytes.insert(bytes.end(),{0x60,1,0x67,0,0x61,0,0xfe,0x05,0});
    bytes.push_back(0x64);
    const auto body=test::Body(bytes);
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
    const auto method=std::get<SemanticMethod>(BuildSemanticMethod(body,kProfile,3));
    const auto proof=std::get<SemanticProof>(VerifySemanticSource(body,method,3,0,{}, CapturedValues::kReject,{}, ClosureWrites::kUnknown,{},CallResultValues::kLanguage));
    const auto graph=std::get<ValueProgram>(ImportNormalValues(body,method,proof,frame));
    Check(!VerifyImportedValues(graph,method,proof,frame));
    std::size_t low=0,high=SemanticBudget{}.bytes;
    while (low<high) {
      const auto mid=low+(high-low)/2;
      const auto result=ImportNormalValues(body,method,proof,frame,{mid,kDefaultSemanticWork});
      if (const auto* error=std::get_if<ValueImportFailure>(&result)) {
        Check(error->kind==ValueImportError::kResourceLimit); low=mid+1;
      } else high=mid;
    }
    Check(low!=0);
    Check(std::holds_alternative<ValueProgram>(ImportNormalValues(body,method,proof,frame,{low,kDefaultSemanticWork})));
    Check(std::get<ValueImportFailure>(ImportNormalValues(body,method,proof,frame,{low-1,kDefaultSemanticWork})).kind==ValueImportError::kResourceLimit);
    Check(std::get<ValueImportFailure>(ImportNormalValues(body,method,proof,frame,{SemanticBudget{}.bytes,0})).kind==ValueImportError::kWorkLimit);
  }
}
}
int main() {
  PhasedCommit();
  CopiesAndBlocks();
  ScaleAndBudget();
  constexpr auto profile = abc::ProfileId::kDynamic1206;
  const auto bytes = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  const auto narrow = test::Body({bytes(abc::Opcode::kGETITERATOR_IMM8), 0, 0x64});
  const auto wide = test::Body({bytes(abc::Opcode::kGETITERATOR_IMM16), 0, 0, 0x64});
  for (const auto* body : {&narrow, &wide}) {
    const auto lifted = std::get<SemanticInstruction>(LiftSemanticInstruction(body->instructions[0], profile, 3));
    Check(std::get<AcquireIterator>(lifted.operation).mode == IteratorMode::kSync);
    Check(lifted.effects.exceptional == ExceptionalCompletion::kMayThrow);
    Check(lifted.effects.before_throw == lifted.effects.normal);
    Check(lifted.effects.exception_cause == ExceptionCause::kUnclassified);
    const auto operands = std::get<ValueOperands>(DescribeValueOperands(lifted));
    Check(operands.count == 1 && operands.write && std::holds_alternative<Accumulator>(*operands.write));
    const auto rule = std::get<detail::ValueRule>(detail::DescribeRegisterValue(lifted, CallResultValues::kLanguage));
    // Primitive/non-callable @@iterator values are real successful results.
    Check(rule.seed == ValueKinds::Language());
    auto bad = body->instructions[0]; bad.decoded.operands[0].kind = abc::OperandKind::kRegister;
    Check(std::holds_alternative<SemanticFailure>(LiftSemanticInstruction(bad, profile, 3)));
  }
  const auto opcode = static_cast<std::uint16_t>(abc::Opcode::kTHROW_IFNOTOBJECT_PREF_V8);
  const auto body = test::Body({static_cast<std::uint8_t>(opcode), static_cast<std::uint8_t>(opcode >> 8), 1, 0x64});
  const auto check = std::get<SemanticInstruction>(LiftSemanticInstruction(body.instructions[0], profile, 3));
  Check(std::get<ObjectCheck>(check.operation).value == abc::RegisterId{1});
  Check(!check.effects.normal_registers.write && check.effects.normal_registers.read_count == 1);
  Check(check.effects.exceptional == ExceptionalCompletion::kMayThrow);
  Check(check.effects.normal == LanguageEffect::kGuard && check.effects.before_throw == LanguageEffect::kGuard);
  Check(std::get<ValueOperands>(DescribeValueOperands(check)).operands[0].role == OperandRole::kGuardValue);
  Check(std::holds_alternative<SemanticFailure>(LiftSemanticInstruction(body.instructions[0], profile, 1)));
}
