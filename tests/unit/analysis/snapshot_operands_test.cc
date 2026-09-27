#include "operand_view.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  const auto body = test::Body({
      static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  for (const std::uint32_t count : {0u, 1u, 200u, 201u, 65536u}) {
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
        2, count + 3, {true, true, true, true}));
    auto budget = SemanticBudget{};
    budget.work = UINT64_MAX;
    const auto built = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206,
        frame.registers, budget, {}, &frame);
    const auto& method = std::get<SemanticMethod>(built);
    auto instruction = method.instructions[0];
    const auto described = OperandView::Build(instruction);
    test::Check(std::holds_alternative<OperandView>(described));
    const auto& view = std::get<OperandView>(described);
    test::Check(view.Count() == count && view.Work() == 1);
    test::Check(view.Write() && std::holds_alternative<Accumulator>(*view.Write()));
    const auto reads = method.ssa.Reads({0});
    test::Check(reads.size() == view.Count());
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto operand = view.At(i);
      test::Check(operand && operand->role == OperandRole::kArgument &&
          operand->purpose == ReadPurpose::kProtocol && operand->read_ordinal == i);
      test::Check(std::get<abc::RegisterId>(operand->value).value ==
          frame.first_parameter.value + i);
      test::Check(operand->read_ordinal == reads[i].ordinal);
      test::Check(std::get<abc::RegisterId>(operand->value) ==
          std::get<abc::RegisterId>(method.ssa.Values()[reads[i].value.value].slot));
    }
    test::Check(!view.At(count) && !view.At(UINT32_MAX));
    test::Check(std::holds_alternative<OperandView>(
        OperandView::Build(instruction, {sizeof(OperandView), view.Work()})));
    test::Check(std::get<OperandViewError>(OperandView::Build(
        instruction, {sizeof(OperandView) - 1, view.Work()})) ==
        OperandViewError::kResourceLimit);
    test::Check(std::get<OperandViewError>(OperandView::Build(
        instruction, {sizeof(OperandView), 0})) == OperandViewError::kWorkLimit);
    const auto valid = instruction;
    const auto rejected = [&] {
      test::Check(std::get<OperandViewError>(OperandView::Build(instruction)) ==
          OperandViewError::kMismatch);
      instruction = valid;
    };
    ++instruction.effects.normal_registers.range->first.value;
    rejected();
    ++instruction.effects.normal_registers.range->count;
    rejected();
    instruction.effects.normal_registers.range.reset();
    rejected();
    instruction.effects.normal_registers.read_count = 1;
    rejected();
    instruction.effects.normal_registers.write = abc::RegisterId{0};
    rejected();
    instruction.effects.normal_registers.write.reset();
    rejected();
    instruction.effects.normal = LanguageEffect::kNone;
    rejected();
    instruction.effects.before_throw = LanguageEffect::kNone;
    rejected();
    instruction.effects.exceptional = ExceptionalCompletion::kImpossible;
    rejected();
  }
  // The view stays compact at the pinned MethodLiteral count limit. Only
  // later admitted SSA/use construction may expand these occurrences.
  constexpr std::uint32_t kRuntimeCountMax = (1u << 28) - 1;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      0, kRuntimeCountMax, {false, false, true, false}));
  const auto instruction = std::get<SemanticInstruction>(LiftSemanticInstruction(
      body.instructions[0], abc::ProfileId::kDynamic1206, frame.registers, &frame));
  const auto plan = std::get<OperandView>(OperandView::Build(
      instruction, {sizeof(OperandView), 1}));
  test::Check(plan.Count() == kRuntimeCountMax && plan.Work() == 1);
  test::Check(plan.At(kRuntimeCountMax - 1)->read_ordinal == kRuntimeCountMax - 1);
  test::Check(!plan.At(kRuntimeCountMax));
}
