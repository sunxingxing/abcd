#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "../../../src/analysis/value_rewrite.h"
#include "analysis/ssa_fixture.h"

#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
namespace {
constexpr auto Wire(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
void Verify(abc::ProfileId profile, std::size_t count, bool snapshot) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 2, {false, false, true, false}));
  std::vector<std::uint8_t> bytes{Wire(abc::Opcode::kLDAI_IMM32), 42, 0, 0, 0,
      Wire(abc::Opcode::kRETURN)};
  const InstructionId dead{2};
  for (std::size_t i = 0; i < count; ++i)
    bytes.push_back(Wire(snapshot ? abc::Opcode::kGETUNMAPPEDARGS : abc::Opcode::kLDUNDEFINED));
  bytes.push_back(Wire(abc::Opcode::kRETURNUNDEFINED));
  const auto body = test::Body(bytes);
  auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, frame.registers, {}, {}, &frame));
  test::Check(method.ssa.Write({0}).has_value());
  test::Check(!method.ssa.Write(dead) && method.ssa.Reads(dead).empty());
  const ArgumentFrameFact arguments{frame};
  const auto prove = [&] { return VerifySemanticSource(body, method, frame.registers, frame.locals,
      {}, CapturedValues::kReject, {}, ClosureWrites::kUnknown, {}, CallResultValues::kUnknown,
      FrameReceiver::kUnknown, &arguments); };
  const auto checked = prove();
  test::Check(std::holds_alternative<SemanticProof>(checked));
  const auto& proof = std::get<SemanticProof>(checked);
  test::Check(proof.environments.before.size() == body.instructions.size());
  test::Check(proof.environments.before[0].has_value());
  for (std::size_t i = dead.value; i < body.instructions.size(); ++i)
    test::Check(!proof.environments.before[i]);
  const auto imported = ImportNormalValues(body, method, proof, frame);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(!VerifyImportedValues(graph, method, proof, frame));
  test::Check(graph.ops[dead.value].life == ValueLife::kRemoved);
  test::Check(graph.origins[dead.value].site.value == dead.value);
  test::Check(graph.blocks.size() == 1);
  auto changed = graph;
  test::Check(std::holds_alternative<ValueSimplifyStats>(SimplifyValues(changed, changed.revision)));
  std::vector<ValueId> identity;
  for (std::uint32_t i = 0; i < graph.values.size(); ++i) identity.push_back({i});
  changed = graph; changed.ops[dead.value].life = ValueLife::kLive;
  test::Check(VerifyImportedValues(changed, method, proof, frame).has_value());
  test::Check(std::get<ValueRewriteError>(VerifyValueRewrite(graph, changed, identity)) == ValueRewriteError::kEffect);
  changed = graph; changed.origins[dead.value].disposition = OriginDisposition::kElidedPure;
  test::Check(std::get<ValueRewriteError>(VerifyValueRewrite(graph, changed, identity)) == ValueRewriteError::kOrigin);
  // Unreachable syntax is still decoded and checked against its original PC
  // and argument ABI. Absence of live SSA does not authorize malformed input.
  ++method.instructions[dead.value].pc.value;
  test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kSsaMismatch);
  --method.instructions[dead.value].pc.value;
  method.instructions[0].effects.normal_registers.write.reset();
  test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kSsaMismatch);
  method.instructions[0].effects.normal_registers.write = Accumulator{};
  if (snapshot) {
    ++method.instructions[dead.value].effects.normal_registers.range->count;
    test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kSsaMismatch);
  }
  std::cout << count << ' ' << snapshot << ' ' << graph.ops.size() << '\n';
}
void BranchPhi(abc::ProfileId profile) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  // The unreachable predecessor lies between the two live arms. Original
  // block 4's phi and block 3's incoming edge must both acquire compact IDs.
  const auto body = test::Body({Wire(abc::Opcode::kLDA_V8), 0,
      Wire(abc::Opcode::kJEQZ_IMM8), 12,
      Wire(abc::Opcode::kLDAI_IMM32), 11, 0, 0, 0,
      Wire(abc::Opcode::kJMP_IMM8), 10,
      Wire(abc::Opcode::kLDUNDEFINED), Wire(abc::Opcode::kJMP_IMM8), 7,
      Wire(abc::Opcode::kLDAI_IMM32), 22, 0, 0, 0,
      Wire(abc::Opcode::kRETURN)});
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, frame.registers, frame.locals));
  test::Check(method.flow.blocks.size() == 5 && !proof.environments.before[4] && !proof.environments.before[5]);
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  test::Check(graph.blocks.size() == 4 && graph.blocks[1].op_count == 4);
  test::Check(!VerifyImportedValues(graph, method, proof, frame));
  std::size_t phis = 0;
  for (std::uint32_t i = 0; i < graph.values.size(); ++i) {
    const auto* input = std::get_if<PhiValue>(&method.ssa.Values()[i].origin);
    if (!input) continue;
    ++phis;
    test::Check(input->block.value == 4);
    test::Check(std::get<PhiDefinition>(graph.values[i].origin).block.value == 3);
    auto wrong = graph; std::get<PhiDefinition>(wrong.values[i].origin).block.value = 2;
    test::Check(VerifyImportedValues(wrong, method, proof, frame).has_value());
  }
  test::Check(phis == 1 && method.ssa.AllInputs().size() == 2);
  test::Check(graph.edges.back().source.value == 2 && graph.edges.back().target.value == 3);
  auto wrong = graph; wrong.edges.back().target.value = 2;
  test::Check(VerifyImportedValues(wrong, method, proof, frame).has_value());
  wrong = graph; ++wrong.blocks[1].op_count;
  test::Check(VerifyImportedValues(wrong, method, proof, frame).has_value());
  const auto scratch = ImportedValueScratch(graph.uses.size(), method.flow.blocks.size());
  test::Check(!VerifyImportedValues(graph, method, proof, frame, {scratch, UINT64_MAX}));
  test::Check(VerifyImportedValues(graph, method, proof, frame, {scratch - 1, UINT64_MAX}).has_value());
  test::Check(std::holds_alternative<ValueSimplifyStats>(SimplifyValues(graph, graph.revision)));
}
}
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    {
    BranchPhi(profile);
    for (const auto count : {1U, 200U, 201U, 400U})
      for (const bool snapshot : {false, true}) Verify(profile, count, snapshot);
    }
}
