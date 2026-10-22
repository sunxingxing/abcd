#include "abcd/analysis/object_spread.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "../../../src/analysis/value_operands.h"
#include "../../../src/analysis/register_value_rule.h"
#include "../../../src/analysis/value_edits.h"
#include <cstdlib>
#include <vector>
namespace { void Check(bool ok) { if(!ok)std::abort(); } }
int main() {
  using namespace abcd; using namespace analysis;
  for(const auto profile:{abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic2400}) {
    for(const auto code:{abc::Opcode::kCOPYDATAPROPERTIES_V8,abc::Opcode::kDEPRECATED_COPYDATAPROPERTIES_PREF_V8_V8}) {
      const auto numeric=static_cast<unsigned>(code);
      std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(numeric)};
      const bool legacy=code==abc::Opcode::kDEPRECATED_COPYDATAPROPERTIES_PREF_V8_V8;
      if(legacy)bytes.push_back(static_cast<std::uint8_t>(numeric>>8));
      bytes.push_back(1);if(legacy)bytes.push_back(2);
      const auto decoded=std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes,profile));
      const auto raw=std::get<CopyProperties>(LiftCopyProperties(decoded,3));
      Check(std::get<abc::RegisterId>(raw.object)==abc::RegisterId{1});
      Check(legacy?std::get<abc::RegisterId>(raw.source)==abc::RegisterId{2}:std::holds_alternative<Accumulator>(raw.source));
      const auto semantic=std::get<SemanticInstruction>(LiftSemanticInstruction({{7},decoded},profile,3));
      const auto operands=std::get<ValueOperands>(DescribeValueOperands(semantic));
      Check(operands.count==2 && operands.write && std::holds_alternative<Accumulator>(*operands.write));
      Check(operands.operands[0].role==OperandRole::kBase && operands.operands[1].role==OperandRole::kStoredValue);
      Check(semantic.effects.normal==semantic.effects.before_throw && semantic.effects.exceptional==ExceptionalCompletion::kMayThrow);
      Check(std::holds_alternative<ValueCopyProperties>(ImportValueOperation(semantic.operation)));
      // Result is the destination, not an unconditional Language fact. Joining
      // exactly read 0 preserves Hole in malformed, but executable, raw inputs.
      const auto rule=std::get<detail::ValueRule>(detail::DescribeRegisterValue(semantic));
      Check(rule.kind==detail::ValueRuleKind::kJoin && rule.input==0 && rule.seed.IsEmpty());
      const auto decoded_rule=std::get<detail::ValueRule>(detail::DescribeRegisterValue(decoded,3));
      Check(decoded_rule.kind==rule.kind && decoded_rule.input==rule.input);
      ValueOp op{};op.operation=ValueCopyProperties{};Check(!CanErasePureOp(op));
      auto bad=decoded;bad.operand_count=0;Check(std::holds_alternative<ScalarError>(LiftCopyProperties(bad,3)));
      bad=decoded;bad.operands[0].kind=abc::OperandKind::kImmediate;Check(std::holds_alternative<ScalarError>(LiftCopyProperties(bad,3)));
      bad=decoded;bad.operands[0].raw_bits=256;Check(std::holds_alternative<ScalarError>(LiftCopyProperties(bad,512)));
      Check(std::holds_alternative<ScalarError>(LiftCopyProperties(decoded,1)));
    }
  }
  for (const auto code : {abc::Opcode::kSETOBJECTWITHPROTO_IMM8_V8, abc::Opcode::kSETOBJECTWITHPROTO_IMM16_V8}) {
    abc::DecodedInstruction d{}; d.opcode=code; d.operand_count=2;
    d.operands[0]={1,abc::OperandKind::kImmediate};
    d.operands[1]={2,abc::OperandKind::kRegister};
    const auto raw=std::get<CopyProperties>(LiftCopyProperties(d,3));
    Check(raw.kind==ObjectMutationKind::kSetPrototype && std::holds_alternative<Accumulator>(raw.object) && std::get<abc::RegisterId>(raw.source).value==2);
    const auto lifted=std::get<SemanticInstruction>(LiftSemanticInstruction({{0},d},abc::ProfileId::kDynamic2400,3));
    const auto value=std::get<ValueCopyProperties>(ImportValueOperation(lifted.operation));
    Check(value.kind==ObjectMutationKind::kSetPrototype);
    const auto inputs=std::get<ValueOperands>(DescribeValueOperands(lifted));
    Check(inputs.count==2 && inputs.operands[0].role==OperandRole::kBase && inputs.operands[1].role==OperandRole::kStoredValue);
    Check(lifted.effects.exceptional==ExceptionalCompletion::kMayThrow);
    auto bad=d;bad.operands[1].raw_bits=3;Check(std::get<ScalarError>(LiftCopyProperties(bad,3))==ScalarError::kRegisterRange);
    bad=d;bad.operands[1].kind=abc::OperandKind::kString;Check(std::get<ScalarError>(LiftCopyProperties(bad,3))==ScalarError::kMalformedInstruction);
    bad=d;bad.operand_count=1;Check(std::get<ScalarError>(LiftCopyProperties(bad,3))==ScalarError::kMalformedInstruction);
  }

}
