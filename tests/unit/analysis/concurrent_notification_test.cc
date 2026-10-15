#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "../../../src/analysis/value_operands.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
using namespace analysis;
using test::Check;
int main() {
  abc::BodyInstruction site{};
  site.decoded.opcode = abc::Opcode::kCALLRUNTIME_NOTIFYCONCURRENTRESULT_PREF_NONE;
  site.decoded.size = 2;
  const auto lifted = LiftSemanticInstruction(site,abc::ProfileId::kDynamic1206,3);
  Check(std::holds_alternative<SemanticInstruction>(lifted));
  const auto& op = std::get<SemanticInstruction>(lifted);
  Check(std::holds_alternative<ConcurrentNotification>(op.operation));
  Check(op.effects.normal == (LanguageEffect::kConcurrentNotification | LanguageEffect::kUserCode));
  Check(op.effects.exceptional == ExceptionalCompletion::kImpossible);
  Check(!op.effects.normal_registers.write && op.effects.normal_registers.read_count == 1);
  const auto operands=std::get<ValueOperands>(DescribeValueOperands(op));
  Check(operands.count==1 && operands.operands[0].role==OperandRole::kReturnValue &&
        operands.operands[0].purpose==ReadPurpose::kProtocol && !operands.write);
  Check(std::holds_alternative<ValueConcurrentNotification>(ImportValueOperation(op.operation)));
  site.decoded.operand_count=1;
  Check(std::holds_alternative<SemanticFailure>(LiftSemanticInstruction(site,abc::ProfileId::kDynamic1206,3)));
}
