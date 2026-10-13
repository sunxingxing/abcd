#include "abcd/analysis/array_spread.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/exception_ssa.h"
#include "../../../src/analysis/value_operands.h"
#include "../../../src/analysis/register_value_rule.h"
#include "../../../src/analysis/value_edits.h"
#include "abcd/analysis/value_import.h"
#include "analysis/ssa_fixture.h"
#include <cstdlib>

namespace {
void Check(bool value) { if (!value) std::abort(); }
}
int main() {
  using namespace abcd;
  using namespace analysis;
  for (const auto opcode : {abc::Opcode::kSTOWNBYVALUE_IMM8_V8_V8, abc::Opcode::kSTOWNBYVALUE_IMM16_V8_V8}) {
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(opcode), 0};
    if (opcode == abc::Opcode::kSTOWNBYVALUE_IMM16_V8_V8) bytes.push_back(0);
    bytes.insert(bytes.end(), {2, 1});
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic2400));
    const auto write = std::get<ComputedOwnWrite>(LiftComputedOwnWrite(decoded, 3));
    Check(write.object == abc::RegisterId{2} && write.key == abc::RegisterId{1});
    const auto semantic = std::get<SemanticInstruction>(LiftSemanticInstruction({{0}, decoded}, abc::ProfileId::kDynamic2400, 3));
    const auto operands = std::get<ValueOperands>(DescribeValueOperands(semantic));
    Check(operands.count == 3 && !operands.write && operands.operands[0].role == OperandRole::kBase &&
        operands.operands[1].role == OperandRole::kKey && operands.operands[2].role == OperandRole::kStoredValue);
    Check(semantic.effects.exceptional == ExceptionalCompletion::kMayThrow &&
        semantic.effects.before_throw == semantic.effects.normal && !semantic.effects.normal_registers.write);
    auto malformed = decoded; malformed.operands[1].kind = abc::OperandKind::kImmediate;
    Check(std::holds_alternative<OwnWriteError>(LiftComputedOwnWrite(malformed, 3)));
  }
  const std::uint8_t bytes[]{0xc6, 2, 1};
  const auto instruction = std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic2400));
  const auto spread = std::get<ArraySpread>(LiftArraySpread(instruction, 3));
  Check(spread.destination == abc::RegisterId{2} && spread.index == abc::RegisterId{1});
  Check(std::get<ArraySpreadError>(LiftArraySpread(instruction, 2)) == ArraySpreadError::kRegisterRange);
  for (unsigned i = 0; i < 2; ++i) {
    auto bad = instruction; bad.operands[i].kind = abc::OperandKind::kImmediate;
    Check(std::get<ArraySpreadError>(LiftArraySpread(bad, 3)) == ArraySpreadError::kMalformedInstruction);
    bad = instruction; bad.operands[i].raw_bits = UINT8_MAX + 1U;
    Check(std::get<ArraySpreadError>(LiftArraySpread(bad, 512)) == ArraySpreadError::kMalformedInstruction);
  }
  auto bad = instruction; bad.operand_count = 1;
  Check(std::get<ArraySpreadError>(LiftArraySpread(bad, 3)) == ArraySpreadError::kMalformedInstruction);
  bad = instruction; bad.opcode = abc::Opcode::kLDGLOBAL;
  Check(std::get<ArraySpreadError>(LiftArraySpread(bad, 3)) == ArraySpreadError::kUnsupportedOpcode);
  const abc::BodyInstruction site{{7}, instruction};
  const auto semantic = std::get<SemanticInstruction>(LiftSemanticInstruction(site, abc::ProfileId::kDynamic2400, 3));
  const auto effects = LanguageEffect::kHeapRead | LanguageEffect::kHeapWrite |
      LanguageEffect::kUserCode | LanguageEffect::kAllocation;
  Check(semantic.effects.normal == effects && semantic.effects.before_throw == effects);
  Check(semantic.effects.exceptional == ExceptionalCompletion::kMayThrow);
  const auto operands = std::get<ValueOperands>(DescribeValueOperands(semantic));
  Check(operands.count == 3 && operands.write && std::holds_alternative<Accumulator>(*operands.write));
  Check(operands.operands[0].role == OperandRole::kDestination &&
        std::get<abc::RegisterId>(operands.operands[0].value) == abc::RegisterId{2});
  Check(operands.operands[1].role == OperandRole::kIndex &&
        std::get<abc::RegisterId>(operands.operands[1].value) == abc::RegisterId{1});
  Check(operands.operands[2].role == OperandRole::kSource &&
        std::holds_alternative<Accumulator>(operands.operands[2].value));
  Check(std::holds_alternative<ValueArraySpread>(ImportValueOperation(semantic.operation)));
  const auto rule = std::get<detail::ValueRule>(detail::DescribeRegisterValue(
      semantic, CallResultValues::kLanguage, FrameReceiver::kUnknown, ArithmeticSemantics::kPinnedRuntime));
  Check(rule.kind == detail::ValueRuleKind::kArrayCursor && rule.input == 1);
  ValueOp forged{}; forged.operation = ValueArraySpread{};
  Check(!CanErasePureOp(forged));
  for (const unsigned count : {1U, 200U, 201U, 400U, 800U}) {
    std::vector<std::uint8_t> bytes;
    for (unsigned i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
          static_cast<std::uint8_t>(abc::Opcode::kSTARRAYSPREAD_V8_V8), 0, 1});
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED));
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
    const auto body = test::Body(bytes);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 3, {}));
    const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
    const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
    Check(!VerifyImportedValues(graph, input, proof, frame));
    for (unsigned i = 0; i < count; ++i) {
      const auto& op = graph.ops[2 * i + 1];
      Check(std::holds_alternative<ValueArraySpread>(op.operation) && op.result && op.use_count == 3);
      Check(graph.values[op.result->value].domain.IsLanguage() && !graph.values[op.result->value].domain.IsNumber());
    }
    for (unsigned role = 0; role < 3; ++role) {
      auto changed = graph;
      std::swap(changed.uses[changed.ops[1].first_use.value + role].operand,
                changed.uses[changed.ops[1].first_use.value + (role + 1) % 3].operand);
      Check(VerifyImportedValues(changed, input, proof, frame).has_value());
    }
    auto changed = graph; changed.ops[1].before_throw = LanguageEffect::kNone;
    Check(VerifyImportedValues(changed, input, proof, frame).has_value());
    changed = graph; changed.ops[1].result.reset();
    Check(VerifyImportedValues(changed, input, proof, frame).has_value());
    // The source iterable remains the incoming accumulator at Before. The
    // cursor exists only at Commit; handler entry defines its own acc.
    auto caught = body;
    const auto end = static_cast<std::uint32_t>(bytes.size());
    caught.instructions.push_back({{end}, body.instructions.back().decoded, {}});
    abc::TryRegion region; region.length = end - 1; region.handler_count = 1;
    caught.tries.push_back(region);
    abc::CatchHandler handler; handler.offset = {end}; handler.length = 1;
    caught.handlers.push_back(handler);
    const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(caught, abc::ProfileId::kDynamic1206, frame.registers));
    IdfQuota quota{kDefaultSemanticWork};
    const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
    unsigned commits = 0;
    for (std::size_t n = 0; n < flow.nodes.size(); ++n) {
      const auto& node = flow.nodes[n];
      if (!std::holds_alternative<ArraySpread>(flow.instructions[node.instruction.value].operation)) continue;
      const ExceptionNodeId id{static_cast<std::uint32_t>(n)};
      if (node.phase == ExceptionPhase::kBefore) {
        Check(node.exception && !ssa.Write(id) && ssa.Reads(id).size() == 3);
      } else if (node.phase == ExceptionPhase::kCommit) {
        Check(!node.exception && ssa.Write(id) && ssa.Reads(id).empty()); ++commits;
      }
    }
    Check(commits == count);
  }
}
