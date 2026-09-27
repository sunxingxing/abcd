#include "call_operands.h"

#include <cstdlib>
#include <limits>

namespace {
using namespace abcd;
using namespace abcd::analysis;
void Check(bool condition) { if (!condition) std::abort(); }
void FixedCalls() {
  CallRegisters arguments{{abc::RegisterId{7}, abc::RegisterId{7}}, 2};
  SemanticInstruction instruction{{0}, ReceiverCall{{7}, arguments}, {}};
  auto& effects = instruction.effects.normal_registers;
  effects.read_count = 4;
  effects.reads = {Accumulator{}, abc::RegisterId{7}, abc::RegisterId{7}, abc::RegisterId{7}};
  effects.write = Accumulator{};
  const auto plan = std::get<CallOperands>(CallOperands::Build(instruction));
  Check(plan.Count() == 4 && plan.Work() == 4);
  Check(plan.At(0)->role == OperandRole::kCallee);
  Check(plan.At(1)->role == OperandRole::kReceiver);
  for (std::uint32_t i = 2; i < 4; ++i) {
    Check(plan.At(i)->role == OperandRole::kArgument);
    Check(plan.At(i)->read_ordinal == i);
    Check(std::get<abc::RegisterId>(plan.At(i)->value).value == 7);
  }
  Check(!plan.At(4));
  effects.reads[2] = abc::RegisterId{8};
  Check(std::get<CallOperandError>(CallOperands::Build(instruction)) == CallOperandError::kSlot);
  effects.reads[2] = abc::RegisterId{7};
  effects.write.reset();
  Check(std::get<CallOperandError>(CallOperands::Build(instruction)) == CallOperandError::kWrite);
}
void Ranges() {
  for (const std::uint16_t count : {0, 200, 201, 1024, 8192, 65535}) {
    SemanticInstruction instruction{{0}, OrdinaryCall{CallRange{{0}, count}}, {}};
    auto& effects = instruction.effects.normal_registers;
    effects.read_count = 1;
    effects.reads[0] = Accumulator{};
    effects.range = CallRange{{0}, count};
    effects.write = Accumulator{};
    const auto plan = std::get<CallOperands>(CallOperands::Build(instruction));
    Check(plan.Count() == count + 1U && plan.Work() == count + 1U);
    if (count != 0) {
      const auto last = *plan.At(count);
      Check(last.role == OperandRole::kArgument && last.read_ordinal == count);
      Check(std::get<abc::RegisterId>(last.value).value == count - 1U);
    }
    Check(std::get<CallOperandError>(CallOperands::Build(instruction, {sizeof(CallOperands), count})) ==
          CallOperandError::kWorkLimit);
    Check(std::get<CallOperandError>(CallOperands::Build(instruction, {0, 0})) ==
          CallOperandError::kResourceLimit);
  }
  constexpr std::uint16_t kLargestEncodedRange = UINT16_MAX;
  SemanticInstruction receiver{
      {0}, ReceiverCall{{0}, CallRange{{1}, kLargestEncodedRange}}, {}};
  auto& effect = receiver.effects.normal_registers;
  effect.read_count = 2;
  effect.reads[0] = Accumulator{};
  effect.reads[1] = abc::RegisterId{0};
  effect.range = CallRange{{1}, kLargestEncodedRange};
  effect.write = Accumulator{};
  const auto wide = std::get<CallOperands>(CallOperands::Build(receiver));
  constexpr std::uint32_t kLastRead = std::uint32_t(kLargestEncodedRange) + 1;
  Check(wide.Count() == kLastRead + 1 &&
        wide.At(kLastRead)->read_ordinal == kLastRead);
  Check(std::get<abc::RegisterId>(wide.At(kLastRead)->value).value ==
        kLargestEncodedRange);
  Check(!wide.At(wide.Count()) && !wide.At(UINT32_MAX));
  Check(std::get<CallOperandError>(CallOperands::Build(
            receiver, {sizeof(CallOperands), wide.Count() - 1})) ==
        CallOperandError::kWorkLimit);
}
void Construction() {
  SemanticInstruction instruction{{0}, ConstructorCall{CallRange{{10}, 3}, {}}, {}};
  auto& effects = instruction.effects.normal_registers;
  effects.range = CallRange{{10}, 3};
  effects.write = Accumulator{};
  const auto plan = std::get<CallOperands>(CallOperands::Build(instruction));
  Check(plan.Count() == 3 && plan.At(0)->role == OperandRole::kConstructor);
  Check(plan.At(1)->role == OperandRole::kArgument);
  Check(std::get<abc::RegisterId>(plan.At(2)->value).value == 12);
  std::get<ConstructorCall>(instruction.operation).inputs.count = 0;
  Check(std::get<CallOperandError>(CallOperands::Build(instruction)) == CallOperandError::kCount);
  std::get<ConstructorCall>(instruction.operation).inputs =
      CallRange{{std::numeric_limits<std::uint32_t>::max()}, 2};
  Check(std::get<CallOperandError>(CallOperands::Build(instruction)) == CallOperandError::kCount);
}
}  // namespace
int main() { FixedCalls(); Ranges(); Construction(); }
