#include "abcd/analysis/value_import.h"

#include "analysis/ssa_fixture.h"
#include "value_edits.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
void Import(std::initializer_list<std::uint8_t> bytes, GlobalLookup lookup) {
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 0, {false, false, false, false}));
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(
      body, abc::ProfileId::kDynamic1206, frame.registers));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, frame.registers, frame.locals));
  const auto imported = ImportNormalValues(body, method, proof, frame);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(!VerifyImportedValues(graph, method, proof, frame));
  const auto& op = graph.ops.front();
  const auto read = std::get<ValueGlobalRead>(op.operation);
  test::Check(read.lookup == lookup && read.name.value == 1);
  test::Check(op.use_count == 0 && op.result.has_value());
  test::Check(op.normal == method.instructions.front().effects.normal);
  test::Check(op.exceptional == ExceptionalCompletion::kMayThrow);
  test::Check(graph.exceptional_exits.size() == 1 && graph.exceptional_exits.front().op == OpId{0});
  // RETURNUNDEFINED leaves the read unused, not unobservable. A getter, absent
  // binding or TDZ still matters; even corrupt empty flags cannot enable DCE.
  test::Check(!graph.values[op.result->value].first_use);
  auto stripped = op;
  stripped.normal = stripped.before_throw = LanguageEffect::kNone;
  stripped.exceptional = ExceptionalCompletion::kImpossible;
  test::Check(!CanErasePureOp(stripped));
  auto changed = graph;
  ++std::get<ValueGlobalRead>(changed.ops[0].operation).name.value;
  test::Check(VerifyImportedValues(changed, method, proof, frame)->kind == ValueImportError::kInputMismatch);
  changed = graph;
  std::get<ValueGlobalRead>(changed.ops[0].operation).lookup =
      lookup == GlobalLookup::kName ? GlobalLookup::kVar : GlobalLookup::kName;
  test::Check(VerifyImportedValues(changed, method, proof, frame)->kind == ValueImportError::kInputMismatch);
}
}  // namespace

int main() {
  // OpenHarmony-v7.0-Release isa.yaml: two TRYLDGLOBALBYNAME cache widths,
  // LDGLOBALVAR's 16-bit cache, and RETURNUNDEFINED. Pool names are identities
  // here, not resolved metadata or evidence of a source binding.
  constexpr std::uint8_t kReadName8 = 0x3f, kReadName16 = 0x8c,
      kReadVar16 = 0x41, kReturnUndefined = 0x65;
  Import({kReadName8, 0, 1, 0, kReturnUndefined}, GlobalLookup::kName);
  Import({kReadName16, 0, 0, 1, 0, kReturnUndefined}, GlobalLookup::kName);
  Import({kReadVar16, 0, 0, 1, 0, kReturnUndefined}, GlobalLookup::kVar);
}
