#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_operands.h"
#include "../../support/analysis/ssa_fixture.h"

#include <algorithm>
#include <array>

using namespace abcd;
using namespace abcd::analysis;
using test::Check;
namespace {
constexpr auto kProfile = abc::ProfileId::kDynamic1206;
void CopiesAndBlocks() {
  // Register copies and split fallthrough corridors retain the internal domain;
  // neither the register number nor adjacency is an admission condition.
  const auto byte = [](abc::Opcode op) { return static_cast<std::uint8_t>(op); };
  for (const std::uint8_t iterator : {0, 2}) {
    const auto body = test::Body({byte(abc::Opcode::kLDA_V8), 1,
        byte(abc::Opcode::kGETPROPITERATOR), byte(abc::Opcode::kSTA_V8), iterator,
        byte(abc::Opcode::kJMP_IMM8), 2,
        byte(abc::Opcode::kLDA_V8), iterator, byte(abc::Opcode::kSTA_V8), 3,
        byte(abc::Opcode::kGETNEXTPROPNAME_V8), 3, byte(abc::Opcode::kRETURN)});
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 4, {}));
    const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, kProfile, 4));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 4, 0));
    const auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
    Check(!VerifyImportedValues(graph, method, proof, frame));
    Check(graph.blocks.size() > 1);
  }
}
void Phased() {
  // Acquisition only is protected. The normal advance is outside the catch.
  auto body = test::Body({0x60,1,0x66,0x61,0,0x36,0,0x64,0x61,2,0x60,2,0xfe,0});
  abc::TryRegion region; region.offset={2}; region.length=1; region.handler_count=1;
  body.tries.push_back(region);
  abc::CatchHandler handler; handler.offset={8}; handler.length=6;
  body.handlers.push_back(handler);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,kProfile,3,{}, {},&frame));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,3,quota));
  std::vector<EntryValueFact> entries;
  for(std::size_t i=0;i<ssa.Values().size();++i)
    if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
      entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,quota,{},PendingExceptionValues::kLanguage));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
  const PhasedValueInput input{flow,ssa,env,values,proof,frame};
  const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph,input)));
  for(const auto& op:graph.ops) if(const auto* enumeration=std::get_if<ValueEnumeration>(&op.operation)) {
    Check(op.result.has_value());
    const auto& origin=std::get<OpResult>(graph.values[op.result->value].origin);
    Check(origin.commit.has_value());
    Check(graph.phases[origin.commit->value].phase==ExceptionPhase::kCommit);
    const auto edge=std::find_if(graph.exceptional_exits.begin(),graph.exceptional_exits.end(),
        [&](const auto& e){return e.op==origin.op;});
    Check(edge!=graph.exceptional_exits.end());
    Check(edge->handler.has_value()==(enumeration->action==EnumerationAction::kAcquire));
  }
  auto bad=graph;
  const auto next=std::find_if(bad.ops.begin(),bad.ops.end(),[](const auto& op){
    const auto* e=std::get_if<ValueEnumeration>(&op.operation);return e&&e->action==EnumerationAction::kNext;});
  next->exceptional=ExceptionalCompletion::kImpossible;
  Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(bad,input)));
}
}
int main() {
  const auto body=test::Body({0x60,1,0x66,0x61,0,0x36,0,0x64});
  const auto acquire=std::get<SemanticInstruction>(LiftSemanticInstruction(body.instructions[1],kProfile,3));
  const auto next=std::get<SemanticInstruction>(LiftSemanticInstruction(body.instructions[3],kProfile,3));
  Check(std::holds_alternative<PropertyIterator>(acquire.operation));
  Check(std::get<NextPropertyName>(next.operation).iterator==abc::RegisterId{0});
  for(const auto* op:{&acquire,&next}) {
    Check(op->effects.exceptional==ExceptionalCompletion::kMayThrow);
    Check(op->effects.before_throw==op->effects.normal);
    Check(op->effects.exception_cause==ExceptionCause::kUnclassified);
    Check(op->effects.normal_registers.write&&std::holds_alternative<Accumulator>(*op->effects.normal_registers.write));
  }
  const auto operands=std::get<ValueOperands>(DescribeValueOperands(next));
  Check(operands.count==1&&operands.operands[0].purpose==ReadPurpose::kProtocol);
  auto malformed=body.instructions[3];malformed.decoded.operands[0].kind=abc::OperandKind::kImmediate;
  Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed,kProfile,3)).kind==SemanticError::kMalformedInstruction);
  Check(std::get<SemanticFailure>(LiftSemanticInstruction(body.instructions[3],kProfile,0)).kind==SemanticError::kMalformedInstruction);
  // Neither a forged entry input nor an escaping internal enumerator is JS.
  for(const auto invalid:{test::Body({0x36,0,0x64}),test::Body({0x60,1,0x66,0x64})}) {
    const auto method=std::get<SemanticMethod>(BuildSemanticMethod(invalid,kProfile,3));
    Check(std::get<SemanticProofFailure>(VerifySemanticSource(invalid,method,3,0)).kind==SemanticProofError::kInternalValue);
  }
  for(const unsigned n:{200U,201U,400U,800U}) {
    std::vector<std::uint8_t> bytes;
    for(unsigned i=0;i<n;++i)bytes.insert(bytes.end(),{0x60,1,0x66,0x61,0,0x36,0});
    bytes.push_back(0x64);
    const auto input_body=test::Body(bytes);
    const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,3,{}));
    const auto method=std::get<SemanticMethod>(BuildSemanticMethod(input_body,kProfile,3));
    const auto proof=std::get<SemanticProof>(VerifySemanticSource(input_body,method,3,0));
    const auto graph=std::get<ValueProgram>(ImportNormalValues(input_body,method,proof,frame));
    Check(!VerifyImportedValues(graph,method,proof,frame));
    for(const auto& op:graph.ops)if(const auto* e=std::get_if<ValueEnumeration>(&op.operation)) {
      Check(op.result.has_value());
      Check(graph.values[op.result->value].domain==(e->action==EnumerationAction::kAcquire?ValueKinds::PropertyEnumerator():ValueKinds::Language()));
    }
    auto bad=graph;bad.ops[1].operation=ValueEnumeration{EnumerationAction::kNext};
    Check(VerifyImportedValues(bad,method,proof,frame).has_value());
    bad = graph;
    bad.uses[bad.ops[3].first_use.value].purpose = ReadPurpose::kOperand;
    Check(VerifyImportedValues(bad, method, proof, frame).has_value());
    bad = graph;
    bad.ops[1].before_throw = LanguageEffect::kNone;
    Check(VerifyImportedValues(bad, method, proof, frame).has_value());
    // Find the actual importer boundary, not an estimated per-item multiplier.
    // All failures below the boundary must be typed resource refusals.
    std::size_t low = 0, high = SemanticBudget{}.bytes;
    while (low < high) {
      const auto mid = low + (high - low) / 2;
      const auto result = ImportNormalValues(input_body, method, proof, frame,
                                            {mid, kDefaultSemanticWork});
      if (const auto* error = std::get_if<ValueImportFailure>(&result)) {
        Check(error->kind == ValueImportError::kResourceLimit);
        low = mid + 1;
      } else high = mid;
    }
    Check(low != 0);
    Check(std::holds_alternative<ValueProgram>(ImportNormalValues(
        input_body, method, proof, frame, {low, kDefaultSemanticWork})));
    Check(std::get<ValueImportFailure>(ImportNormalValues(
        input_body, method, proof, frame, {low - 1, kDefaultSemanticWork})).kind ==
        ValueImportError::kResourceLimit);
    Check(std::get<ValueImportFailure>(ImportNormalValues(
        input_body, method, proof, frame, {SemanticBudget{}.bytes, 0})).kind ==
        ValueImportError::kWorkLimit);
  }
  CopiesAndBlocks();
  Phased();
}
