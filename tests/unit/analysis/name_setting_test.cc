#include "abcd/analysis/own_write.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/isa_catalog.h"
#include "../../../src/analysis/value_operands.h"
#include "../../../src/source/evaluation_failure.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <vector>

namespace {
using namespace abcd;
using namespace analysis;
using test::Check;
constexpr std::array kOpcodes{
    abc::Opcode::kSTOWNBYNAMEWITHNAMESET_IMM8_ID16_V8,
    abc::Opcode::kSTOWNBYNAMEWITHNAMESET_IMM16_ID16_V8,
    abc::Opcode::kSTOWNBYVALUEWITHNAMESET_IMM8_V8_V8,
    abc::Opcode::kSTOWNBYVALUEWITHNAMESET_IMM16_V8_V8};
std::vector<std::uint8_t> Encoding(abc::Opcode opcode) {
  const auto* spec = FindPinnedInstruction(opcode);
  Check(spec != nullptr);
  std::vector<std::uint8_t> bytes(spec->size);
  bytes[0] = static_cast<std::uint8_t>(opcode);
  return bytes;
}
PropertyNameSetting Naming(const SemanticOperation& operation) {
  if (const auto* named = std::get_if<OwnWrite>(&operation)) return named->name_setting;
  return std::get<ComputedOwnWrite>(operation).name_setting;
}
void Instructions() {
  for (const auto opcode : kOpcodes) {
    const auto bytes = Encoding(opcode);
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic1206));
    const auto lift = [&](const abc::DecodedInstruction& instruction, std::uint64_t frame = 1) {
      return LiftSemanticInstruction({{0}, instruction}, abc::ProfileId::kDynamic1206, frame);
    };
    const auto raw = std::get<SemanticInstruction>(lift(decoded));
    Check(Naming(raw.operation) == PropertyNameSetting::kPropertyKey);
    Check(raw.effects.exceptional == ExceptionalCompletion::kMayThrow);
    Check(raw.effects.normal == raw.effects.before_throw);
    Check(!raw.effects.normal_registers.write);
    const auto operands = std::get<ValueOperands>(DescribeValueOperands(raw));
    const bool named = std::holds_alternative<OwnWrite>(raw.operation);
    Check(operands.count == (named ? 2 : 3) && !operands.write);
    Check(operands.operands[0].role == OperandRole::kBase);
    Check(operands.operands[operands.count - 1].role == OperandRole::kStoredValue);
    if (!named) Check(operands.operands[1].role == OperandRole::kKey);
    auto imported = ImportValueOperation(raw.operation);
    if (named) {
      const auto value = std::get<ValueOwnWrite>(imported);
      Check(value.kind == OwnWriteKind::kStore && value.name_setting == PropertyNameSetting::kPropertyKey);
      auto mutated = value; mutated.name_setting = PropertyNameSetting::kNone;
      Check(mutated != value);
    } else {
      const auto value = std::get<ValueComputedOwnWrite>(imported);
      Check(value.name_setting == PropertyNameSetting::kPropertyKey);
      Check(value != ValueComputedOwnWrite{});
    }
    // A name-neutral plan cannot silently consume this extra heap mutation.
    ValueProgram graph;
    graph.origins.push_back({{0}, {0}, {}});
    graph.ops.push_back({imported, {0}, 0, {0}, 0, {}, raw.effects.normal,
                        raw.effects.before_throw, raw.effects.exceptional, {0}});
    const auto failure = source::NameSettingRefusal(graph, {0});
    Check(failure && failure->evaluation.plan_cause == source::EvaluationPlanCause{source::LiteralError::kNameSettingProof});
    graph.ops[0].operation = ValueNamedWrite{{0}};
    Check(!source::NameSettingRefusal(graph, {0}));
    // Width, operand kind, and frame mutations are malformed, never a different
    // property operation. The zero PC is a real location, not absence.
    Check(std::holds_alternative<SemanticFailure>(lift(decoded, 0)));
    for (std::size_t i = 0; i < decoded.operand_count; ++i) {
      auto bad = decoded; bad.operands[i].raw_bits = UINT64_MAX;
      Check(std::get<SemanticFailure>(lift(bad)).kind == SemanticError::kMalformedInstruction);
      bad = decoded; bad.operands[i].kind = abc::OperandKind::kMethod;
      Check(std::get<SemanticFailure>(lift(bad)).kind == SemanticError::kMalformedInstruction);
    }
  }
}
void ImportedMutation() {
  for (const auto opcode : kOpcodes) {
    std::vector<std::uint8_t> bytes{
        static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED),
        static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0};
    const auto instruction = Encoding(opcode);
    bytes.insert(bytes.end(), instruction.begin(), instruction.end());
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
    const auto body = test::Body(bytes);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
    const auto semantic = std::get<SemanticMethod>(BuildSemanticMethod(
        body, abc::ProfileId::kDynamic1206, frame.registers));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(
        body, semantic, frame.registers, frame.locals));
    auto graph = std::get<ValueProgram>(ImportNormalValues(body, semantic, proof, frame));
    Check(!VerifyImportedValues(graph, semantic, proof, frame));
    bool changed = false;
    for (auto& op : graph.ops) {
      if (auto* named = std::get_if<ValueOwnWrite>(&op.operation)) {
        named->name_setting = PropertyNameSetting::kNone;
        changed = true;
      }
      if (auto* computed = std::get_if<ValueComputedOwnWrite>(&op.operation)) {
        computed->name_setting = PropertyNameSetting::kNone;
        changed = true;
      }
    }
    Check(changed && VerifyImportedValues(graph, semantic, proof, frame).has_value());
  }
}
void Scaling() {
  for (const auto n : {200,201,400,800}) {
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(abc::Opcode::kLDUNDEFINED)};
    for (int i = 0; i < n; ++i) {
      const auto instruction = Encoding(kOpcodes[static_cast<std::size_t>(i) % kOpcodes.size()]);
      bytes.insert(bytes.end(), instruction.begin(), instruction.end());
    }
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED));
    const auto body = test::Body(bytes);
    const auto run = [&](SemanticBudget budget) { return BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 1, budget); };
    const auto good = run({}); Check(std::holds_alternative<SemanticMethod>(good));
    Check(std::get<SemanticMethod>(good).instructions.size() == static_cast<std::size_t>(n+2));
    for (const bool work : {false,true}) {
      std::size_t lo=0,hi=64*1024*1024;
      const auto allowance = [&](std::size_t limit) { SemanticBudget b; if(work)b.work=limit;else b.bytes=limit;return b; };
      while(lo<hi) { const auto mid=lo+(hi-lo)/2; if(std::holds_alternative<SemanticMethod>(run(allowance(mid))))hi=mid;else lo=mid+1; }
      Check(lo>0 && std::holds_alternative<SemanticMethod>(run(allowance(lo))));
      const auto fail=std::get<SemanticFailure>(run(allowance(lo-1)));
      Check(fail.kind == SemanticError::kResourceLimit || fail.kind == SemanticError::kSsa);
    }
  }
}
}
int main() { Instructions(); ImportedMutation(); Scaling(); }
