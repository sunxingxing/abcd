#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/semantic_validation.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  const auto body = test::Body({
      static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  for (const auto profile : {abc::ProfileId::kDynamic1202,
                            abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    for (const std::uint32_t count : {0u, 1u, 200u, 201u, 65536u}) {
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, count, {false, false, true, false}));
      auto budget = SemanticBudget{};
      budget.work = UINT64_MAX;
      const auto built = BuildSemanticMethod(body, profile, frame.registers,
          budget, {}, &frame);
      test::Check(std::holds_alternative<SemanticMethod>(built));
      const auto& method = std::get<SemanticMethod>(built);
      test::Check(std::holds_alternative<ArgumentSnapshot>(method.instructions[0].operation));
      const auto& effects = method.instructions[0].effects;
      test::Check(effects.normal ==
          (LanguageEffect::kArgumentsRead | LanguageEffect::kAllocation));
      test::Check(effects.before_throw == effects.normal &&
          effects.exceptional == ExceptionalCompletion::kMayThrow);
      const auto reads = method.ssa.Reads({0});
      test::Check(reads.size() == count);
      for (std::uint32_t i = 0; i < count; ++i) {
        test::Check(reads[i].ordinal == i);
        const auto& value = method.ssa.Values()[reads[i].value.value];
        test::Check(std::get<abc::RegisterId>(value.slot).value == i);
        test::Check(std::holds_alternative<MethodEntry>(value.origin));
      }
      test::Check(method.ssa.Reads({1})[0].value == *method.ssa.Write({0}));
      // A layout is not an actual-count/tail ownership or source certificate.
      test::Check(std::get<SemanticProofFailure>(VerifySemanticSource(
          body, method, frame.registers, 0)).kind ==
          SemanticProofError::kArgumentOwner);
    }
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 0)));
    auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 1, {false, false, true, false}));
    const auto overwritten = test::Body({0x62, 9, 0, 0, 0, 0x61, 0,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x61, 0,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x64});
    const auto built = BuildSemanticMethod(overwritten, profile, 1, {}, {}, &frame);
    const auto& method = std::get<SemanticMethod>(built);
    test::Check(method.ssa.Reads({2})[0].value == *method.ssa.Write({1}));
    test::Check(method.ssa.Reads({4})[0].value == *method.ssa.Write({3}));
    test::Check(method.ssa.Write({2}) != method.ssa.Write({4}));
    const auto loop = test::Body({
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        0x61, 0, 0x4d, 0xfd});
    const auto looping = BuildSemanticMethod(loop, profile, 1, {}, {}, &frame);
    const auto& loop_ssa = std::get<SemanticMethod>(looping).ssa;
    const auto carried = loop_ssa.Reads({0})[0].value;
    test::Check(std::holds_alternative<PhiValue>(loop_ssa.Values()[carried.value].origin));
    test::Check(loop_ssa.Inputs(carried).size() == 2);
    test::Check(loop_ssa.Inputs(carried)[1].value == *loop_ssa.Write({1}));
    const auto branch = test::Body({0x60, 0, 0x4f, 11,
        0x62, 11, 0, 0, 0, 0x61, 1, 0x4d, 9,
        0x62, 22, 0, 0, 0, 0x61, 1,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x64});
    const auto pair = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 2, {false, false, true, false}));
    const auto branched = BuildSemanticMethod(branch, profile, 2, {}, {}, &pair);
    const auto& branch_ssa = std::get<SemanticMethod>(branched).ssa;
    const auto joined = branch_ssa.Reads({7})[1].value;
    test::Check(std::holds_alternative<PhiValue>(branch_ssa.Values()[joined.value].origin));
    test::Check(branch_ssa.Inputs(joined).size() == 2);
    test::Check(branch_ssa.Inputs(joined)[0].value == *branch_ssa.Write({3}));
    test::Check(branch_ssa.Inputs(joined)[1].value == *branch_ssa.Write({6}));
    const auto hidden = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(2, 4, {true, true, true, true}));
    const auto with_hidden = BuildSemanticMethod(body, profile,
        hidden.registers, {}, {}, &hidden);
    const auto& hidden_ssa = std::get<SemanticMethod>(with_hidden).ssa;
    test::Check(hidden_ssa.Reads({0}).size() == hidden.parameters);
    test::Check(std::get<abc::RegisterId>(hidden_ssa.Values()[
        hidden_ssa.Reads({0})[0].value.value].slot) == hidden.first_parameter);
    frame.extra_arguments = false;
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 1, {}, {}, &frame)));
    frame.extra_arguments = true;
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 2, {}, {}, &frame)));
    auto malformed = body.instructions[0];
    malformed.decoded.operand_count = 1;
    test::Check(std::holds_alternative<SemanticFailure>(
        LiftSemanticInstruction(malformed, profile, 1, &frame)));
    frame.first_parameter.value = 1;
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 1, {}, {}, &frame)));
  }
  constexpr std::uint32_t kRuntimeCountMax = (1u << 28) - 1;
  const auto huge = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      0, kRuntimeCountMax, {false, false, true, false}));
  auto bounded = SemanticBudget{};
  bounded.work = 100;
  test::Check(std::holds_alternative<SemanticFailure>(BuildSemanticMethod(
      body, abc::ProfileId::kDynamic1206, huge.registers, bounded, {}, &huge)));
  test::Check(std::get<SemanticFailure>(BuildSemanticMethod(body,
      abc::ProfileId::kUnknown, huge.registers, {}, {}, &huge)).kind ==
      SemanticError::kUnsupportedProfile);
}
