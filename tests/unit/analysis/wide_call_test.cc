#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/derived_constructor.h"
#include "call_operands.h"
#include "../../../src/source/evaluation_failure.h"
#include <cstdlib>
using namespace abcd;
using namespace abcd::analysis;
namespace {
void Check(bool b) { if (!b) std::abort(); }
abc::DecodedInstruction Range(abc::Opcode opcode, std::uint16_t count,
                             std::uint32_t first, bool named = false) {
  abc::DecodedInstruction d{}; d.opcode = opcode; d.operand_count = named ? 3 : 2;
  d.operands[0].kind = abc::OperandKind::kImmediate; d.operands[0].raw_bits = count;
  if (named) {d.operands[1].kind=abc::OperandKind::kString; d.operands[1].raw_bits=123;}
  d.operands[named ? 2 : 1].kind=abc::OperandKind::kRegister;
  d.operands[named ? 2 : 1].raw_bits=first; return d;
}
}
int main() {
  source::EvaluationEvidence evidence;
  ValueProgram g;g.origins.push_back({{0},{7},{}});
  g.ops.push_back({ValueInvoke{InvokeReceiver::kUndefined,300,CallArgumentForm::kRegisters,RangeCallSemantics::kIsaLayout}});
  for(auto contract:{source::SourceContract::kLanguageSourceV1,source::SourceContract::kLanguageSourceV7}) {
    const auto refusal=source::WideRangeRefusal(g,{0},contract);
    Check(refusal.has_value());
  }
  Check(!source::WideRangeRefusal(g,{0},source::SourceContract::kLanguageSourceV8));

  for (std::uint16_t n : {200,201,256,300,400,800}) {
    for (bool named : {false,true}) {
      auto d=Range(named ? abc::Opcode::kWIDE_CALLTHISRANGEWITHNAME_PREF_IMM16_ID16_V8 : abc::Opcode::kWIDE_CALLTHISRANGE_PREF_IMM16_V8,n,7,named);
      const auto c=std::get<ReceiverCall>(LiftReceiverCall(d,n+8));
      Check(c.receiver.value==7 && c.Count()==n && c.At(n-1)->value==n+7U);
      Check(std::get<CallError>(LiftReceiverCall(d,n+7))==CallError::kRegisterRange);
      d.operands[0].raw_bits=65536;
      Check(std::get<CallError>(LiftReceiverCall(d,UINT64_MAX))==CallError::kMalformedInstruction);
    }
    auto d=Range(abc::Opcode::kWIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8,n,7);
    const auto c=std::get<SuperRange>(LiftSuperInstruction(d,n+7));
    Check(c.callee==SuperCalleeSource::kAccumulator && c.first.value==7 && c.count==n);
    SemanticInstruction instruction{{0},c,{}};
    auto& e=instruction.effects.normal_registers;
    e.read_count=1; e.reads[0]=Accumulator{}; e.write=Accumulator{}; e.range=RegisterRange{{7},n};
    const auto p=std::get<CallOperands>(CallOperands::Build(instruction,{sizeof(CallOperands),n+1U}));
    Check(p.Count()==n+1U && p.At(0)->role==OperandRole::kConstructor);
    Check(std::get<abc::RegisterId>(p.At(n)->value).value==n+6U);
    Check(std::get<CallOperandError>(CallOperands::Build(instruction,{sizeof(CallOperands),n}))==CallOperandError::kWorkLimit);
    Check(std::get<CallOperandError>(CallOperands::Build(instruction,{sizeof(CallOperands)-1,n+1U}))==CallOperandError::kResourceLimit);
    e.reads[0]=abc::RegisterId{7};
    Check(std::get<CallOperandError>(CallOperands::Build(instruction))==CallOperandError::kSlot);
  }
}
