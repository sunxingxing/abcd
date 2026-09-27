#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/binding_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/function_abi.h"
#include "../../../src/source/tree_flow.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class ReturnedEntry { kParameter, kCallable, kNewTarget };
void Run(std::uint32_t count, abc::ProfileId profile,
         ReturnedEntry returned = ReturnedEntry::kParameter, bool nop = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kLocal = 0, kOther = 1, kFunction = 2, kNewTarget = 3, kParameter = 5;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kMOV_V8_V8), kLocal, kFunction};
  for (std::uint32_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kLocal, kLocal});
  bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kOther, kNewTarget});
  if (nop) bytes.push_back(op(abc::Opcode::kNOP));
  const auto elided = count + 2 + (nop ? 1 : 0);
  const auto protected_pc = static_cast<std::uint32_t>(bytes.size());
  std::uint8_t returned_register = kParameter;
  switch (returned) {
    case ReturnedEntry::kParameter: break;
    case ReturnedEntry::kCallable: returned_register = kLocal; break;
    case ReturnedEntry::kNewTarget: returned_register = kOther; break;
  }
  // The getter is observable even though its result is discarded. Returning a
  // hidden ABI entry is the counterexample to treating all ABI copies as dead.
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      op(abc::Opcode::kLDA_V8), returned_register,
      op(abc::Opcode::kRETURN), op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size() - 1);
  abc::TryRegion region; region.offset = {protected_pc}; region.length = handler_pc - protected_pc;
  region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 4, {true, true, false, true}));
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
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto binding = BuildPhasedBindingPlan(graph, evaluation, 1);
  if (returned != ReturnedEntry::kParameter) {
    const auto observed = returned == ReturnedEntry::kCallable ? 0 : count + 1;
    test::Check(evaluation.operations[observed].kind == EvaluationKind::kRoot);
    test::Check(std::get<BindingError>(binding) == BindingError::kUnsupportedEntry);
    return;
  }
  test::Check(std::holds_alternative<BindingPlan>(binding));
  for (std::size_t i = 0; i < elided; ++i) {
    test::Check(evaluation.operations[i].kind == EvaluationKind::kElidedPure);
    test::Check(!evaluation.operations[i].temporary && !evaluation.operations[i].consumer);
  }
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  const auto checked = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked})));
  test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work})));
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(graph, {0})) == EvaluationError::kResourceLimit);
  auto changed = evaluation;
  const auto load = elided, getter = load + 1;
  test::Check(changed.operations[getter].kind == EvaluationKind::kRoot);
  changed.operations[getter] = {EvaluationKind::kElidedPure};
  test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)) == EvaluationError::kOwnership);
  changed = evaluation; changed.operations[load] = {EvaluationKind::kElidedPure};
  test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)) == EvaluationError::kOwnership);
  changed = evaluation; changed.operations.back() = {EvaluationKind::kElidedPure};
  test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(graph, changed)) == EvaluationError::kOwnership);
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  const ExceptionTreeInput context{body, input, regions, control};
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(1); syntax.names = {u"value"};
  std::vector<ExpressionConstant> constants(graph.ops.size()); constants[getter] = NameId{0};
  // The shared tree builder invokes independent graph checks at several
  // boundaries. Keep the geometric fixture, with an explicit test-only budget.
  constexpr std::uint64_t kTreeWork = 16 * kDefaultSemanticWork;
  const SemanticBudget tree_budget{SemanticBudget{}.bytes, kTreeWork};
  auto tree = std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(
      graph, context, evaluation, syntax, {constants, 1}, tree_budget)).tree;
  test::Check(std::get<FunctionAbiError>(BuildFunctionAbi(graph, tree, frame, 1)) == FunctionAbiError::kEntry);
  const auto abi = std::get<ConstructedAbi>(BuildPhasedFunctionAbi(graph, evaluation, tree, frame, 1));
  test::Check(abi.abi.parameters.size() == 1 && abi.abi.length == 1);
  test::Check(std::holds_alternative<ConstructedAbi>(BuildPhasedFunctionAbi(
      graph, evaluation, tree, frame, 1, {SemanticBudget{}.bytes, abi.work})));
  test::Check(std::get<FunctionAbiError>(BuildPhasedFunctionAbi(
      graph, evaluation, tree, frame, 1, {SemanticBudget{}.bytes, abi.work - 1})) == FunctionAbiError::kWorkLimit);
  test::Check(std::get<FunctionAbiError>(BuildPhasedFunctionAbi(graph, changed, tree, frame, 1)) ==
      FunctionAbiError::kEvaluation);
  test::Check(tree.operations.size() == graph.ops.size());
  for (const auto& placement : tree.operations) {
    if (placement.operation.value >= elided) continue;
    test::Check(std::holds_alternative<AnchorId>(placement.node));
    const auto& anchor = tree.anchors[std::get<AnchorId>(placement.node).value];
    test::Check(std::get<OriginalSite>(tree.origins[anchor.origin.value]).pc ==
        body.instructions[placement.operation.value].offset);
  }
  // Moving a deleted PC across actual source evaluation is still forbidden.
  ++std::get<BlockBoundary>(tree.anchors.front().position).index;
  const auto misplaced = VerifyExceptionTreeFlow(graph, context, evaluation, tree, tree_budget);
  test::Check(std::get<TreeFlowError>(misplaced) == TreeFlowError::kSequence);
}
}  // namespace
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    for (const auto count : {1U, 200U, 201U, 402U, 1608U}) Run(count, profile);
    Run(2, profile, ReturnedEntry::kCallable);
    Run(2, profile, ReturnedEntry::kNewTarget);
    Run(2, profile, ReturnedEntry::kParameter, true);
  }
}
