#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Shape {
  kReturn, kCatch, kObserve, kLate, kBranch, kCatchObserve, kNested, kNestedCatch,
  kNestedCatchObserve, kNestedDispatch, kNestedShared, kNestedSnapshot
};
std::uint64_t Run(abc::ProfileId profile, Shape shape = Shape::kReturn,
                  std::uint32_t copies = 0, std::uint32_t fields = 1,
                  abc::Opcode define = abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kObject = 0, kParameter = 4;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), kObject};
  const bool nested = shape == Shape::kNested || shape == Shape::kNestedCatch ||
      shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedShared || shape == Shape::kNestedSnapshot;
  const bool caught = shape == Shape::kCatch || shape == Shape::kCatchObserve ||
      shape == Shape::kNestedCatch || shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedSnapshot;
  constexpr std::uint8_t kSaved = 2, kSnapshotInput = 6, kSnapshotPrelude = 4;
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.begin(),
      {op(abc::Opcode::kLDA_V8), kSnapshotInput, op(abc::Opcode::kSTA_V8), kSaved});
  const auto initialized_pc = static_cast<std::uint32_t>(bytes.size());
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.end(),
      {op(abc::Opcode::kLDTRUE), op(abc::Opcode::kSTA_V8), kSaved});
  for (std::uint32_t i = 0; i < copies; ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kObject, kObject});
  if (shape == Shape::kObserve) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  if (shape == Shape::kBranch) bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 2});
  if (!nested) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
  if (shape == Shape::kLate) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  for (std::uint32_t i = 0; i < fields; ++i) {
    constexpr std::uint8_t kInner = 1;
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), kInner});
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),
        static_cast<std::uint8_t>(shape == Shape::kNestedSnapshot ? kSnapshotInput : kParameter + 1)});
    bytes.insert(bytes.end(), {op(define), 0});
    if (define == abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8) bytes.push_back(0);
    bytes.insert(bytes.end(), {0, 0, nested ? kInner : kObject});
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kInner,
        op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, kObject});
  }
  if (shape == Shape::kNestedShared) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, kObject});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject, op(abc::Opcode::kRETURN)});
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  if (shape == Shape::kCatchObserve || shape == Shape::kNestedCatchObserve) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject});
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kSaved});
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {shape == Shape::kCatchObserve || shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch ? initialized_pc : 0};
    if (shape == Shape::kNestedSnapshot) region.offset = {kSnapshotPrelude};
    region.length = catch_pc - region.offset.value; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = bytes.size() - catch_pc;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(shape == Shape::kNestedSnapshot ? 3 : nested ? 2 : 1, 4, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, ssa_work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto result = BuildPhasedEvaluationPlan(graph);
  if (shape == Shape::kObserve || shape == Shape::kBranch || shape == Shape::kCatchObserve ||
      shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedShared || shape == Shape::kNestedSnapshot) {
    test::Check(std::get<EvaluationError>(result) == EvaluationError::kUnsupportedOperation);
    return 0;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(result));
  const auto& evaluation = std::get<EvaluationPlan>(result);
  test::Check(evaluation.literals && evaluation.literals->objects.size() == (nested ? fields + 1 : 1));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  const auto& literal = *evaluation.literals;
  test::Check(literal.definitions.size() == (nested ? fields * 2 : fields));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedLiteralPlan(graph, literal, evaluation)));
  test::Check(std::holds_alternative<LiteralError>(VerifyLiteralPlan(graph, literal)));
  auto changed = literal; ++changed.revision.value;
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)) == LiteralError::kRevision);
  changed = literal; --changed.objects[0].count;
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)) == LiteralError::kCoverage);
  if (nested) {
    changed = literal;
    const auto at = std::find_if(changed.nested.begin(), changed.nested.end(), [](const auto& use) { return use.has_value(); });
    test::Check(at != changed.nested.end());
    --(*at)->value;
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)));
    changed = literal; std::swap(changed.definitions.front(), changed.definitions.back());
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, changed, evaluation)));
  }
  auto aliases = evaluation; aliases.aliases.back().value = graph.values.size();
  test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, aliases)) == LiteralError::kFlow);
  test::Check(std::holds_alternative<LiteralPlan>(BuildPhasedLiteralPlan(graph, evaluation,
      {SemanticBudget{}.bytes, literal.work})));
  test::Check(std::get<LiteralError>(BuildPhasedLiteralPlan(graph, evaluation,
      {SemanticBudget{}.bytes, literal.work - 1})) == LiteralError::kWorkLimit);
  test::Check(std::get<LiteralError>(BuildPhasedLiteralPlan(graph, evaluation, {0})) == LiteralError::kResourceLimit);
  if (shape == Shape::kCatch || shape == Shape::kNestedCatch) {
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(1); syntax.names = {u"__proto__"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (const auto id : literal.definitions) constants[id.value] = NameId{0};
    const ExceptionTreeInput context{body, input, regions, control};
    // This exercises repeated independent tree/phase checks as well as the
    // initializer proof. Keep its explicit allowance separate from the exact
    // initializer-work tests above; production defaults are unchanged.
    constexpr SemanticBudget kTreeBudget{};
    const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, {constants, 1}, kTreeBudget);
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
    const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
    test::Check(tree.properties.size() == (nested ? fields * 2 : fields));
    test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
        graph, context, evaluation, tree, {constants, 1}, kTreeBudget)));
  }
  return literal.work;
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    for (const auto shape : {Shape::kReturn, Shape::kCatch, Shape::kObserve, Shape::kLate, Shape::kBranch,
                             Shape::kCatchObserve, Shape::kNested, Shape::kNestedCatch, Shape::kNestedCatchObserve,
                             Shape::kNestedSnapshot, Shape::kNestedDispatch, Shape::kNestedShared}) Run(profile, shape);
    for (const auto opcode : {abcd::abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8,
        abcd::abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abcd::abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8})
      Run(profile, Shape::kCatch, 0, 1, opcode);
    for (const auto copies : {1U, 200U, 201U, 402U, 804U}) Run(profile, Shape::kCatch, copies);
    for (const auto fields : {2U, 200U, 201U, 400U, 800U})
      Run(profile, Shape::kNestedCatch, 0, fields);
    std::uint64_t previous = 0;
    for (const auto fields : {200U, 201U, 402U, 804U}) {
      const auto work = Run(profile, Shape::kCatch, 0, fields);
      if (fields > 201) abcd::test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
