#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/phase_evaluation.h"
#include "../../../src/source/call_plan.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Corridor {
  kPure, kEffect, kJump, kBranch, kChangedCatch, kStableCatch, kWrongReceiver, kDuplicate,
  kArgument, kArguments, kReversedArguments, kHandlerChange, kTypeofArgument,
  kClosure, kClosureEnvironment, kClosureHandler
};
std::uint64_t Run(std::uint32_t count, abc::ProfileId profile, Corridor corridor = Corridor::kPure,
                  bool computed = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kCallee = 0, kArgument = 1, kSecondArgument = 2, kThis = 5, kParameter = 6;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kMOV_V8_V8), kArgument, kParameter,
      op(abc::Opcode::kLDA_V8), kParameter,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
      op(abc::Opcode::kSTA_V8), kCallee};
  if (computed) {
    constexpr std::uint8_t kKey = 7;
    bytes = {op(abc::Opcode::kMOV_V8_V8), kArgument, kParameter,
        op(abc::Opcode::kLDAI_IMM32), kKey, 0, 0, 0,
        op(abc::Opcode::kLDOBJBYVALUE_IMM8_V8), 0, kParameter,
        op(abc::Opcode::kSTA_V8), kCallee};
  }
  constexpr OpId kProperty{2};
  for (std::uint32_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kArgument, kArgument});
  switch (corridor) {
    case Corridor::kClosure: case Corridor::kClosureEnvironment: case Corridor::kClosureHandler:
      bytes.insert(bytes.end(), {op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0,
          op(abc::Opcode::kSTA_V8), kArgument});
      if (corridor == Corridor::kClosureEnvironment)
        bytes.insert(bytes.end(), {op(abc::Opcode::kNEWLEXENV_IMM8), 1, op(abc::Opcode::kPOPLEXENV)});
      break;
    case Corridor::kEffect:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0}); break;
    case Corridor::kJump:
      bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 2}); break;
    case Corridor::kBranch:
      bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 2}); break;
    case Corridor::kChangedCatch:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDTRUE), op(abc::Opcode::kSTA_V8), kArgument}); break;
    case Corridor::kDuplicate:
      // Two effective callee uses must never duplicate a getter.
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee,
          op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, kParameter, kArgument}); break;
    case Corridor::kArgument: case Corridor::kArguments: case Corridor::kReversedArguments:
    case Corridor::kTypeofArgument:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
      if (corridor == Corridor::kTypeofArgument)
        for (std::uint32_t i = 0; i < count; ++i)
          bytes.insert(bytes.end(), {op(abc::Opcode::kTYPEOF_IMM8), 0});
      bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
      if (corridor == Corridor::kArguments || corridor == Corridor::kReversedArguments)
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
            op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), kSecondArgument});
      break;
    case Corridor::kPure: case Corridor::kStableCatch: case Corridor::kWrongReceiver:
    case Corridor::kHandlerChange: break;
  }
  const auto before_call = static_cast<std::uint32_t>(bytes.size());
  if (corridor == Corridor::kArguments || corridor == Corridor::kReversedArguments) {
    const bool reversed = corridor == Corridor::kReversedArguments;
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee,
        op(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8), 0, kParameter,
        reversed ? kSecondArgument : kArgument, reversed ? kArgument : kSecondArgument,
        op(abc::Opcode::kRETURN)});
  } else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee,
      op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0,
      corridor == Corridor::kWrongReceiver ? kThis : kParameter, kArgument, op(abc::Opcode::kRETURN)});
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  const bool snapshot = corridor == Corridor::kChangedCatch || corridor == Corridor::kStableCatch;
  if (snapshot) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument});
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.length = handler_pc; region.handler_count = 1;
  if (corridor == Corridor::kHandlerChange || corridor == Corridor::kClosureHandler) region.length = before_call;
  abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = snapshot ? 3 : 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 4, {true, true, false, true}));
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
  std::vector<FunctionBinding> functions;
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueFunctionCreation>(graph.ops[i].operation))
      functions.push_back({OpId{static_cast<std::uint32_t>(i)}, FunctionOwnerId{1}, {}});
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject, functions));
  const bool accepted = corridor == Corridor::kPure || corridor == Corridor::kStableCatch ||
      corridor == Corridor::kArgument || corridor == Corridor::kArguments || corridor == Corridor::kTypeofArgument ||
      corridor == Corridor::kClosure;
  const bool fused = accepted || corridor == Corridor::kWrongReceiver;
  test::Check((evaluation.operations[kProperty.value].kind == EvaluationKind::kInline) == fused);
  for (const auto& function : functions)
    test::Check((evaluation.operations[function.creation.value].kind == EvaluationKind::kInline) == accepted);
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  const auto calls = BuildPhasedCallPlan(graph, evaluation);
  test::Check(std::holds_alternative<CallPlan>(calls) == accepted);
  if (corridor == Corridor::kWrongReceiver)
    test::Check(std::get<CallPlanError>(calls) == CallPlanError::kReceiver);
  const auto checked = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked})));
  test::Check(std::get<EvaluationError>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work}, IncomingCells::kReject, functions)));
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work - 1}, IncomingCells::kReject, functions)) == EvaluationError::kWorkLimit);
  test::Check(std::get<EvaluationError>(VerifyPhaseExpressions(graph, evaluation, {0})) == EvaluationError::kResourceLimit);
  if (!fused) {
    auto forged = evaluation;
    const auto call = snapshot ? graph.ops.size() - 4 : graph.ops.size() - 3;
    forged.operations[kProperty.value] = {EvaluationKind::kInline, graph.ops[call].first_use, {}};
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    for (const auto& function : functions) {
      forged = evaluation;
      forged.operations[function.creation.value] = {EvaluationKind::kInline,
          UseId{graph.ops[call].first_use.value + 2}, {}}; // callee, receiver, callback.
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  } else {
    auto malformed = graph;
    const auto consumer = *evaluation.operations[kProperty.value].consumer;
    const auto call = std::get<OpId>(graph.uses[consumer.value].owner);
    malformed.ops[call.value].use_count = 0;
    test::Check(std::get<EvaluationError>(VerifyPhaseExpressions(malformed, evaluation, {})) ==
        EvaluationError::kInvalidGraph);
    auto forged = evaluation;
    forged.operations[kProperty.value].consumer = graph.ops[kProperty.value].first_use;
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation;
    forged.operations[kProperty.value].consumer = UseId{static_cast<std::uint32_t>(graph.uses.size())};
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
  }
  return std::get<PhaseEvaluationCost>(VerifyPhaseExpressions(graph, evaluation, {})).work;
}
}  // namespace
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    std::uint32_t prior_count = 0;
    std::uint64_t prior_work = 0, prior_wrapped_work = 0;
    constexpr std::uint32_t kScalingStart = 200;
    for (const auto count : {1U, kScalingStart, 201U, 402U, 1608U}) {
      const auto work = Run(count, profile);
      const auto wrapped_work = Run(count, profile, Corridor::kTypeofArgument);
      Run(count, profile, Corridor::kClosure);
      if (prior_count >= kScalingStart) {
        test::Check(work <= prior_work * (count / prior_count + 1));
        test::Check(wrapped_work <= prior_wrapped_work * (count / prior_count + 1));
      }
      prior_count = count; prior_work = work; prior_wrapped_work = wrapped_work;
    }
    Run(2, profile, Corridor::kPure, true);
    for (const auto corridor : {Corridor::kEffect, Corridor::kJump, Corridor::kBranch,
        Corridor::kChangedCatch, Corridor::kStableCatch, Corridor::kWrongReceiver, Corridor::kDuplicate,
        Corridor::kArgument, Corridor::kArguments, Corridor::kReversedArguments, Corridor::kHandlerChange,
        Corridor::kTypeofArgument, Corridor::kClosure, Corridor::kClosureEnvironment, Corridor::kClosureHandler})
      Run(2, profile, corridor);
  }
}
