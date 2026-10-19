#include "abcd/analysis/semantic_ir.h"
#include "../../../src/analysis/operand_view.h"
#include "abcd/analysis/value_import.h"
#include <cstdlib>
namespace { void Check(bool ok) { if (!ok) std::abort(); } }
int main() {
  using namespace abcd; using namespace analysis;
  const std::uint8_t bytes[]{static_cast<std::uint8_t>(abc::Opcode::kAPPLY_IMM8_V8_V8),0,2,1};
  const auto decoded=std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes,abc::ProfileId::kDynamic2400));
  const auto raw=std::get<SemanticInstruction>(LiftSemanticInstruction({{0},decoded},abc::ProfileId::kDynamic2400,3));
  const auto& call=std::get<ReceiverCall>(raw.operation);
  Check(call.form==CallArgumentForm::kApplyArray && call.receiver==abc::RegisterId{2} && call.Count()==1 && call.At(0)==abc::RegisterId{1});
  const auto inputs=std::get<OperandView>(OperandView::Build(raw));
  Check(inputs.Count()==3 && inputs.At(0)->role==OperandRole::kCallee && inputs.At(1)->role==OperandRole::kReceiver && inputs.At(2)->role==OperandRole::kArgument);
  Check(std::holds_alternative<Accumulator>(*inputs.Write()));
  const auto effects=LanguageEffect::kAllocation|LanguageEffect::kHeapRead|LanguageEffect::kHeapWrite|LanguageEffect::kUserCode;
  Check(raw.effects.normal==effects && raw.effects.before_throw==effects && raw.effects.exceptional==ExceptionalCompletion::kMayThrow);
  const auto value=std::get<ValueInvoke>(ImportValueOperation(raw.operation));
  Check(value.form==CallArgumentForm::kApplyArray && value.arguments==1 && value.receiver==InvokeReceiver::kExplicit);
  auto changed=value;changed.form=CallArgumentForm::kRegisters;Check(changed!=value);
  for(unsigned i=0;i<decoded.operand_count;++i) {
    auto bad=decoded;bad.operands[i].raw_bits=UINT64_MAX;
    Check(std::get<SemanticFailure>(LiftSemanticInstruction({{0},bad},abc::ProfileId::kDynamic2400,3)).kind==SemanticError::kMalformedInstruction);
  }
  Check(std::get<SemanticFailure>(LiftSemanticInstruction({{0},decoded},abc::ProfileId::kDynamic2400,2)).kind==SemanticError::kMalformedInstruction);
}
