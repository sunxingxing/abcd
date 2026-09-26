#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

#include <limits>

#include "analysis/ssa_fixture.h"
#include "value_edits.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
void Import(bool method_form, bool wide) {
  // OpenHarmony-v7.0-Release isa.yaml: both DEFINEFUNC/DEFINEMETHOD cache widths.
  constexpr std::uint8_t kLoad = 0x60, kFunction = 0x33, kMethod = 0x34,
      kWideFunction = 0x74, kWideMethod = 0xbe, kReturnUndefined = 0x65,
      kCacheLow = 0x23, kCacheHigh = 1, kTargetLow = 7, kLength = 3;
  const auto opcode = method_form ? (wide ? kWideMethod : kMethod) : (wide ? kWideFunction : kFunction);
  std::vector<std::uint8_t> code{kLoad, 0, opcode, kCacheLow};
  if (wide) code.push_back(kCacheHigh);
  code.insert(code.end(), {kTargetLow, 0, kLength, kReturnUndefined});
  const auto body = test::Body(code);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  test::Check(!VerifyImportedValues(graph, input, proof, frame));
  const auto& op = graph.ops[1];
  const auto& creation = std::get<ValueFunctionCreation>(op.operation);
  test::Check(creation.kind == (method_form ? CreationKind::kMethod : CreationKind::kFunction));
  test::Check(creation.home_object == (method_form ? HomeObjectInput::kAccumulator : HomeObjectInput::kCurrentFunction));
  test::Check(creation.method.value == kTargetLow && creation.length == kLength);
  constexpr auto kByteBits = std::numeric_limits<std::uint8_t>::digits;
  test::Check(creation.cache_slot == (wide ? (kCacheHigh << kByteBits) | kCacheLow : kCacheLow));
  test::Check(op.result && !graph.values[op.result->value].first_use);
  test::Check(op.use_count == (method_form ? 1U : 0U));
  if (method_form) {
    const auto& use = graph.uses[op.first_use.value];
    test::Check(use.role == OperandRole::kHomeObject && use.purpose == ReadPurpose::kProtocol &&
        use.input && use.input->ordinal == 0);
    auto changed = graph;
    changed.uses[op.first_use.value].purpose = ReadPurpose::kOperand;
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kOperandMismatch);
  }
  test::Check(graph.exceptional_exits.size() == 1 && graph.exceptional_exits[0].op == OpId{1});
  auto stripped = op;
  stripped.normal = stripped.before_throw = LanguageEffect::kNone;
  stripped.exceptional = ExceptionalCompletion::kImpossible;
  test::Check(!CanErasePureOp(stripped));
  const auto reject = [&](auto change) {
    auto changed = graph; change(std::get<ValueFunctionCreation>(changed.ops[1].operation));
    test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  };
  reject([](auto& item) { ++item.method.value; });
  reject([](auto& item) { ++item.length; });
  reject([](auto& item) { ++item.cache_slot; });
  reject([&](auto& item) { item.kind = method_form ? CreationKind::kFunction : CreationKind::kMethod; });
  reject([&](auto& item) { item.home_object = method_form ? HomeObjectInput::kCurrentFunction : HomeObjectInput::kAccumulator; });
  auto simplified = graph;
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(simplified, graph.revision)));
  test::Check(simplified.ops[1].life == ValueLife::kLive &&
      std::get<ValueFunctionCreation>(simplified.ops[1].operation) == creation);
}
}  // namespace
int main() {
  for (const bool method : {false, true}) for (const bool wide : {false, true}) Import(method, wide);
}
