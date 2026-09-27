#include "../../../src/source/evaluation_plan.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;

ValueProgram Import(const abc::MethodBody& body, std::uint32_t parameters,
                    abc::ProfileId profile, SemanticBudget budget) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      0, parameters, {false, false, true, false}));
  const ArgumentFrameFact invocation{frame};
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(
      body, profile, frame.registers, budget, {}, &frame));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method,
      frame.registers, frame.locals, budget, CapturedValues::kReject, {},
      ClosureWrites::kUnknown, {}, CallResultValues::kUnknown,
      FrameReceiver::kUnknown, &invocation));
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame, budget));
  test::Check(!VerifyImportedValues(graph, method, proof, frame, budget));
  test::Check(std::holds_alternative<ValueSimplifyStats>(
      SimplifyValues(graph, graph.revision, budget)));
  return graph;
}

void CheckPlan(const abc::MethodBody& body, std::uint32_t parameters,
               abc::ProfileId profile) {
  auto budget = SemanticBudget{};
  budget.work = UINT64_MAX;
  const auto graph = Import(body, parameters, profile, budget);
  const auto built = BuildEvaluationPlan(graph, budget);
  test::Check(std::holds_alternative<EvaluationPlan>(built));
  const auto& plan = std::get<EvaluationPlan>(built);
  test::Check(plan.arguments == graph.arguments);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, plan, budget)));
  auto forged = plan;
  forged.arguments.reset();
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, forged, budget)) ==
      EvaluationError::kOwnership);
  auto unowned = graph;
  unowned.arguments.reset();
  test::Check(std::get<EvaluationError>(BuildEvaluationPlan(unowned, budget)) == EvaluationError::kOwnership);
  auto limited = budget;
  limited.work = 0;
  test::Check(std::get<EvaluationError>(BuildEvaluationPlan(graph, limited)) == EvaluationError::kWorkLimit);
  limited = budget;
  limited.bytes = 0;
  test::Check(std::get<EvaluationError>(BuildEvaluationPlan(graph, limited)) == EvaluationError::kResourceLimit);
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (!std::holds_alternative<ValueArgumentSnapshot>(op.operation)) continue;
    const auto shape = DescribeEvaluation(op.operation);
    test::Check(shape && shape->inputs == InputEvaluation::kConditionalArgument &&
        shape->NeedsSnapshot(OperandRole::kArgument));
    auto changed = graph;
    ++std::get<ValueArgumentSnapshot>(changed.ops[i].operation).slots;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(changed, plan, budget)));
    changed = graph;
    changed.ops[i].before_throw = LanguageEffect::kNone;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(changed, plan, budget)));
    if (op.use_count == 0) continue;
    changed = graph;
    changed.uses[op.first_use.value].purpose = ReadPurpose::kOperand;
    test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(changed, plan, budget)) ==
        EvaluationError::kProtocolOperand);
    changed = graph;
    ++changed.uses[op.first_use.value].input->ordinal;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(changed, plan, budget)));
    const auto& use = graph.uses[op.first_use.value];
    const auto value = std::get<ValueId>(use.operand);
    changed = graph;
    changed.values[value.value].domain = ValueKinds::Hole();
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(changed, plan, budget)));
    // Even a single-use pure wrapper must not carry its effectful subtree into
    // a conditional slot. Forge that exact motion for the independent checker.
    if (const auto* result = std::get_if<OpResult>(&graph.values[value.value].origin)) {
      test::Check(plan.operations[result->op.value].kind == EvaluationKind::kRoot);
      forged = plan;
      forged.operations[result->op.value] = {EvaluationKind::kInline, op.first_use, {}};
      test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged, budget)));
    }
  }
}
}  // namespace

int main() {
  using namespace abcd;
  const auto body = test::Body({
      static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  for (const auto profile : {abc::ProfileId::kDynamic1202,
                            abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    for (const std::uint32_t count : {0u, 1u, 200u, 201u, 65536u})
      CheckPlan(body, count, profile);
    const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
    constexpr std::uint8_t kParameter = 0, kCache = 0, kNameLow = 0, kNameHigh = 0;
    const auto getter = test::Body({op(abc::Opcode::kLDA_V8), kParameter,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), kCache, kNameLow, kNameHigh,
        op(abc::Opcode::kSTA_V8), kParameter,
        op(abc::Opcode::kGETUNMAPPEDARGS), op(abc::Opcode::kRETURN)});
    CheckPlan(getter, 1, profile);
    const auto repeated = test::Body({op(abc::Opcode::kGETUNMAPPEDARGS),
        op(abc::Opcode::kSTA_V8), kParameter,
        op(abc::Opcode::kGETUNMAPPEDARGS), op(abc::Opcode::kRETURN)});
    CheckPlan(repeated, 1, profile);
    constexpr std::uint8_t kBackToSnapshot = 0xfd;  // PC 3 -> PC 0, signed i8.
    const auto loop = test::Body({op(abc::Opcode::kGETUNMAPPEDARGS),
        op(abc::Opcode::kSTA_V8), kParameter, op(abc::Opcode::kJMP_IMM8), kBackToSnapshot});
    CheckPlan(loop, 1, profile);
    constexpr std::uint8_t kSecondParameter = 1;
    constexpr std::uint8_t kElseDelta = 11, kJoinDelta = 9;
    constexpr std::uint8_t kThenValue = 11, kElseValue = 22;
    const auto branch = test::Body({op(abc::Opcode::kLDA_V8), kParameter,
        op(abc::Opcode::kJEQZ_IMM8), kElseDelta,
        op(abc::Opcode::kLDAI_IMM32), kThenValue, 0, 0, 0,
        op(abc::Opcode::kSTA_V8), kSecondParameter,
        op(abc::Opcode::kJMP_IMM8), kJoinDelta,
        op(abc::Opcode::kLDAI_IMM32), kElseValue, 0, 0, 0,
        op(abc::Opcode::kSTA_V8), kSecondParameter,
        op(abc::Opcode::kGETUNMAPPEDARGS), op(abc::Opcode::kRETURN)});
    CheckPlan(branch, 2, profile);
  }
}
