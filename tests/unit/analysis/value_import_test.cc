#include "abcd/analysis/value_import.h"

#include "analysis/ssa_fixture.h"

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  auto body = test::Body({0x60, 0, 0x61, 1, 0x60, 1, 0x64});
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(0, 2, {false, false, false, false}));
  const auto built = BuildSemanticMethod(body, abc::ProfileId::kDynamic1206,
                                        frame.registers);
  const auto& method = std::get<SemanticMethod>(built);
  const auto checked = VerifySemanticSource(body, method, frame.registers, frame.locals);
  const auto& proof = std::get<SemanticProof>(checked);
  const auto imported = ImportNormalValues(body, method, proof, frame);
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(graph.ops.size() == body.instructions.size());
  test::Check(graph.values.size() == method.ssa.Values().size());
  test::Check(graph.input.values.size() == graph.values.size());
  test::Check(graph.input.sites.size() == body.instructions.size());
  test::Check(!VerifyValueUses(graph, graph.uses.size()));
  test::Check(graph.exceptional_exits.empty());
  std::size_t occurrences = 0;
  for (const auto& use : graph.uses) {
    if (!use.input) continue;
    ++occurrences;
    const auto* value = std::get_if<ValueId>(&use.operand);
    test::Check(value != nullptr);
    test::Check(graph.input.values[value->value] == *value);
  }
  test::Check(occurrences == method.ssa.AllReads().size());
  const auto memory_limited = ImportNormalValues(body, method, proof, frame, {0, 1000});
  test::Check(std::get<ValueImportFailure>(memory_limited).kind == ValueImportError::kResourceLimit);
  const auto work_limited = ImportNormalValues(body, method, proof, frame, {1024 * 1024, 0});
  test::Check(std::get<ValueImportFailure>(work_limited).kind == ValueImportError::kWorkLimit);
  body.tries.push_back({{0}, 1, 0, 0});
  test::Check(std::get<ValueImportFailure>(
      ImportNormalValues(body, method, proof, frame)).kind == ValueImportError::kExceptionFlow);
  body.tries.clear();
  body.instructions[0].offset.value = 1;
  test::Check(std::get<ValueImportFailure>(
      ImportNormalValues(body, method, proof, frame)).kind == ValueImportError::kInputMismatch);
}
