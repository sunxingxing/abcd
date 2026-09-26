#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

#include "analysis/ssa_fixture.h"
#include "value_edits.h"
#include "value_operands.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
enum class Width { kNibble, kByte, kWord };
void Import(Width width) {
  // Locked v7 ISA: allocate a local environment, store an ABI language value,
  // load that exact cell, pop the environment, return the preserved accumulator.
  constexpr std::uint8_t kPush = 0x09, kWide = 0xfd, kWidePush = 0x02,
      kLoadRegister = 0x60, kStoreNibble = 0x3d, kLoadNibble = 0x3c,
      kStoreByte = 0x8b, kLoadByte = 0x8a, kStoreWord = 0x0d, kLoadWord = 0x0c,
      kPop = 0x69, kReturn = 0x64;
  const bool word = width == Width::kWord;
  std::vector<std::uint8_t> code = word ? std::vector<std::uint8_t>{kWide, kWidePush, 1, 1}
                                      : std::vector<std::uint8_t>{kPush, 1};
  code.insert(code.end(), {kLoadRegister, 0});
  switch (width) {
    case Width::kNibble: code.insert(code.end(), {kStoreNibble, 0, kLoadNibble, 0}); break;
    case Width::kByte: code.insert(code.end(), {kStoreByte, 0, 0, kLoadByte, 0, 0}); break;
    case Width::kWord: code.insert(code.end(), {kWide, kStoreWord, 0, 0, 0, 1,
                                               kWide, kLoadWord, 0, 0, 0, 1}); break;
  }
  code.insert(code.end(), {kPop, kReturn});
  const auto body = test::Body(code);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
  const auto input = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, input, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, input, proof, frame));
  test::Check(!VerifyImportedValues(graph, input, proof, frame));
  const auto& allocation = graph.ops[0];
  const auto& push = std::get<ValueEnvironmentPush>(allocation.operation);
  test::Check(push.slots == (word ? 257 : 1) && !push.scope);
  test::Check(allocation.result && graph.values[allocation.result->value].domain == ValueKinds::Environment());
  test::Check(allocation.before_throw != allocation.normal && graph.exceptional_exits.size() == 1);
  const auto& store = graph.ops[2];
  const auto& load = graph.ops[3];
  test::Check(std::get<ValueLexicalStore>(store.operation).address.slot.value == (word ? 256 : 0));
  test::Check(std::get<ValueLexicalLoad>(load.operation).address.slot.value == (word ? 256 : 0));
  test::Check(!store.result && store.use_count == 1 && load.result && load.use_count == 0);
  test::Check(graph.uses[store.first_use.value].role == OperandRole::kStoredValue);
  test::Check(graph.values[load.result->value].domain.IsLanguage());
  test::Check(std::holds_alternative<ValueEnvironmentPop>(graph.ops[4].operation) && !graph.ops[4].result);
  test::Check(std::get<ValueId>(graph.uses[graph.ops[5].first_use.value].operand) == *load.result);
  auto changed = graph;
  ++std::get<ValueLexicalLoad>(changed.ops[3].operation).address.depth.value;
  test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  changed = graph; ++std::get<ValueLexicalStore>(changed.ops[2].operation).address.slot.value;
  test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  changed = graph; std::get<ValueEnvironmentPush>(changed.ops[0].operation).scope = abc::PoolIndex{1};
  test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  changed = graph; changed.ops[4].operation = ValueNop{};
  test::Check(VerifyImportedValues(changed, input, proof, frame)->kind == ValueImportError::kInputMismatch);
  changed = graph;
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(changed, graph.revision)));
  for (const auto index : {0U, 2U, 3U, 4U}) {
    test::Check(changed.ops[index].life == ValueLife::kLive);
    auto stripped = graph.ops[index];
    stripped.normal = stripped.before_throw = LanguageEffect::kNone;
    stripped.exceptional = ExceptionalCompletion::kImpossible;
    test::Check(!CanErasePureOp(stripped));
  }
}
void Metadata() {
  const auto push = std::get<ValueEnvironmentPush>(ImportValueOperation(PushEnvironment{257, abc::PoolIndex{513}}));
  test::Check(push.slots == 257 && push.scope == abc::PoolIndex{513});
  test::Check(std::holds_alternative<ValueCurrentEnvironment>(ImportValueOperation(CurrentEnvironment{})));
  // Historical register stores retain the register source, not an invented acc read.
  const LexicalStore stored{{{1}, {2}}, abc::RegisterId{3}};
  SemanticInstruction input{{0}, stored, {}, {}};
  input.effects.normal_registers.reads[0] = abc::RegisterId{3};
  input.effects.normal_registers.read_count = 1;
  const auto operands = std::get<ValueOperands>(DescribeValueOperands(input));
  test::Check(operands.count == 1 && !operands.write &&
      std::get<abc::RegisterId>(operands.operands[0].value).value == 3);
}
}  // namespace
int main() { Import(Width::kNibble); Import(Width::kByte); Import(Width::kWord); Metadata(); }
