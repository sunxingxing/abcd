#include "abcd/analysis/value_import.h"

#include "analysis/ssa_fixture.h"
#include "value_edits.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
enum class Form { kOrdinary, kReceiver, kConstruct };
void Import(Form form, std::uint8_t arguments) {
  // v7 ISA: LDA, CALLRANGE/CALLTHISRANGE/NEWOBJRANGE, RETURNUNDEFINED.
  // Construction's encoded count includes its constructor occurrence.
  constexpr std::uint8_t kLoad = 0x60, kCall = 0x73, kReceiver = 0x31,
      kConstruct = 0x08, kReturnUndefined = 0x65;
  const auto opcode = form == Form::kOrdinary ? kCall :
      form == Form::kReceiver ? kReceiver : kConstruct;
  const std::uint8_t encoded = form == Form::kConstruct ? arguments + 1 : arguments;
  const std::uint8_t first = form == Form::kOrdinary ? 1 : 0;
  const auto body = test::Body({kLoad, 0, opcode, 0, encoded, first, kReturnUndefined});
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, static_cast<std::uint32_t>(arguments) + 3, {false, false, false, false}));
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(
      body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  test::Check(!VerifyImportedValues(graph, method, proof, frame));
  const auto scratch = ImportedValueScratch(graph.uses.size());
  test::Check(!VerifyImportedValues(graph, method, proof, frame, {scratch, 1000000}));
  test::Check(VerifyImportedValues(graph, method, proof, frame, {scratch - 1, 1000000})->kind ==
              ValueImportError::kResourceLimit);
  const auto& call = graph.ops[1];
  if (form == Form::kConstruct) {
    test::Check(std::get<ValueConstruct>(call.operation).arguments == arguments);
  } else {
    const auto invoke = std::get<ValueInvoke>(call.operation);
    test::Check(invoke.arguments == arguments);
    test::Check(invoke.receiver == (form == Form::kReceiver ? InvokeReceiver::kExplicit : InvokeReceiver::kUndefined));
  }
  const auto prefix = form == Form::kReceiver ? 2U : 1U;
  test::Check(call.use_count == arguments + prefix);
  test::Check(graph.uses[call.first_use.value].role ==
      (form == Form::kConstruct ? OperandRole::kConstructor : OperandRole::kCallee));
  for (std::uint32_t i = prefix; i < call.use_count; ++i) {
    const auto& use = graph.uses[call.first_use.value + i];
    test::Check(use.role == OperandRole::kArgument && use.input->ordinal == i);
  }
  test::Check(call.result && !graph.values[call.result->value].first_use);
  test::Check(graph.exceptional_exits.size() == 1 && graph.exceptional_exits[0].op == OpId{1});
  auto erased_effects = call;
  erased_effects.normal = erased_effects.before_throw = LanguageEffect::kNone;
  erased_effects.exceptional = ExceptionalCompletion::kImpossible;
  test::Check(!CanErasePureOp(erased_effects));
  auto invalid = graph;
  invalid.uses[call.first_use.value].role = OperandRole::kStoredValue;
  test::Check(VerifyImportedValues(invalid, method, proof, frame)->kind == ValueImportError::kOperandMismatch);
  invalid = graph;
  std::visit([](auto& operation) {
    using T = std::decay_t<decltype(operation)>;
    if constexpr (std::is_same_v<T, ValueInvoke> || std::is_same_v<T, ValueConstruct>) ++operation.arguments;
  }, invalid.ops[1].operation);
  test::Check(VerifyImportedValues(invalid, method, proof, frame)->kind == ValueImportError::kInputMismatch);
}
}  // namespace
int main() {
  for (const auto form : {Form::kOrdinary, Form::kReceiver, Form::kConstruct})
    for (const auto count : {0, 1, 3, 200, 201}) Import(form, count);
}
