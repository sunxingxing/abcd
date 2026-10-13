#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/phase_evaluation.h"
#include "../../../src/source/call_plan.h"
#include "analysis/ssa_fixture.h"
#include "abcd/analysis/exception_locals.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Corridor {
  kPure, kEffect, kJump, kBranch, kChangedCatch, kStableCatch, kWrongReceiver, kDuplicate,
  kArgument, kArguments, kReversedArguments, kHandlerChange, kTypeofArgument,
  kClosure, kClosureEnvironment, kClosureHandler, kPureResult, kClosurePureResult,
  kLiteral, kNestedLiteral, kLiteralArguments, kLiteralReversed, kLiteralHandler, kLexical, kLexicalGuard, kLexicalWrite
};
std::uint64_t Run(std::uint32_t count, abc::ProfileId profile, Corridor corridor = Corridor::kPure,
                  bool computed = false, bool global = false) {
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
  if (global) {
    // The call argument is independent of the effectful receiver base. The
    // base has one source occurrence despite its second raw VM receiver use.
    bytes[2] = kThis;
    bytes.insert(bytes.begin() + 3, {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kSTA_V8), kParameter});
  }
  const bool lexical = corridor == Corridor::kLexical || corridor == Corridor::kLexicalGuard || corridor == Corridor::kLexicalWrite;
  std::uint32_t try_begin = 0;
  if (lexical) {
    const std::vector<std::uint8_t> prelude{op(abc::Opcode::kNEWLEXENV_IMM8), 1,
        op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0};
    try_begin = static_cast<std::uint32_t>(prelude.size());
    bytes.insert(bytes.begin(), prelude.begin(), prelude.end());
  }
  for (std::uint32_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kArgument, kArgument});
  switch (corridor) {
    case Corridor::kLexical: case Corridor::kLexicalGuard: case Corridor::kLexicalWrite:
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0});
      if (corridor == Corridor::kLexicalGuard) {
        constexpr auto kGuard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
        constexpr unsigned kByteBits = 8;
        bytes.insert(bytes.end(), {static_cast<std::uint8_t>(kGuard),
            static_cast<std::uint8_t>(kGuard >> kByteBits), 0, 0});
      }
      bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
      if (corridor == Corridor::kLexicalWrite)
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDTRUE), op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
      break;
    case Corridor::kLiteral: case Corridor::kNestedLiteral: case Corridor::kLiteralArguments:
    case Corridor::kLiteralReversed: case Corridor::kLiteralHandler: {
      const auto create = [&](std::uint8_t object) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), object});
      };
      const auto field = [&](std::uint8_t object) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 1, 0, object});
      };
      create(kArgument);
      if (corridor == Corridor::kNestedLiteral) create(kSecondArgument);
      for (std::uint32_t i = 0; i < count; ++i) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
            op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
        field(corridor == Corridor::kNestedLiteral ? kSecondArgument : kArgument);
      }
      if (corridor == Corridor::kNestedLiteral) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kSecondArgument}); field(kArgument);
      }
      if (corridor == Corridor::kLiteralArguments || corridor == Corridor::kLiteralReversed) {
        create(kSecondArgument);
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter,
            op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0}); field(kSecondArgument);
      }
      break;
    }
    case Corridor::kClosure: case Corridor::kClosureEnvironment: case Corridor::kClosureHandler:
    case Corridor::kClosurePureResult:
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
    case Corridor::kHandlerChange: case Corridor::kPureResult: break;
  }
  const auto before_call = static_cast<std::uint32_t>(bytes.size());
  if (corridor == Corridor::kArguments || corridor == Corridor::kReversedArguments ||
      corridor == Corridor::kLiteralArguments || corridor == Corridor::kLiteralReversed) {
    const bool reversed = corridor == Corridor::kReversedArguments || corridor == Corridor::kLiteralReversed;
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee,
        op(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8), 0, kParameter,
        reversed ? kSecondArgument : kArgument, reversed ? kArgument : kSecondArgument,
        op(abc::Opcode::kRETURN)});
  } else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee,
      op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0,
      corridor == Corridor::kWrongReceiver ? kThis : kParameter, kArgument, op(abc::Opcode::kRETURN)});
  const bool pure_result = corridor == Corridor::kPureResult || corridor == Corridor::kClosurePureResult;
  if (pure_result) {
    test::Check(bytes.back() == op(abc::Opcode::kRETURN)); bytes.pop_back();
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {op(abc::Opcode::kTYPEOF_IMM8), 0});
    bytes.push_back(op(abc::Opcode::kRETURN));
  }
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  const bool snapshot = corridor == Corridor::kChangedCatch || corridor == Corridor::kStableCatch;
  if (snapshot) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument});
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {try_begin}; region.length = handler_pc - try_begin; region.handler_count = 1;
  if (corridor == Corridor::kHandlerChange || corridor == Corridor::kClosureHandler || corridor == Corridor::kLiteralHandler)
    region.length = before_call;
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
  std::vector<ExceptionLoadFact> loads;
  if (lexical) loads = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env)).loads;
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, loads));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  std::optional<OpId> property;
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation) ||
        std::holds_alternative<ValuePropertyRead>(graph.ops[i].operation)) {
      property = OpId{static_cast<std::uint32_t>(i)}; break;
    }
  test::Check(property.has_value());
  const auto kProperty = *property;
  std::vector<FunctionBinding> functions;
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (std::holds_alternative<ValueFunctionCreation>(graph.ops[i].operation))
      functions.push_back({OpId{static_cast<std::uint32_t>(i)}, FunctionOwnerId{1}, {}});
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject, functions));
  const bool accepted = (lexical && corridor != Corridor::kLexicalWrite) || corridor == Corridor::kPure || corridor == Corridor::kStableCatch ||
      corridor == Corridor::kArgument || corridor == Corridor::kArguments || corridor == Corridor::kTypeofArgument ||
      corridor == Corridor::kClosure || pure_result || corridor == Corridor::kLiteral ||
      corridor == Corridor::kNestedLiteral || corridor == Corridor::kLiteralArguments;
  const bool fused = accepted || corridor == Corridor::kWrongReceiver;
  test::Check((evaluation.operations[kProperty.value].kind == EvaluationKind::kInline) == fused);
  for (const auto& function : functions)
    test::Check((evaluation.operations[function.creation.value].kind == EvaluationKind::kInline) == accepted);
  test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  const auto calls = BuildPhasedCallPlan(graph, evaluation);
  test::Check(std::holds_alternative<CallPlan>(calls) == accepted);
  if (corridor == Corridor::kWrongReceiver)
    test::Check(std::get<CallPlanFailure>(calls) == CallPlanError::kReceiver);
  const auto checked = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked})));
  test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work}, IncomingCells::kReject, functions)));
  test::Check(std::get<EvaluationFailure>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work - 1}, IncomingCells::kReject, functions)) == EvaluationError::kWorkLimit);
  test::Check(std::get<EvaluationFailure>(VerifyPhaseExpressions(graph, evaluation, {0})) == EvaluationError::kResourceLimit);
  if (!fused) {
    auto forged = evaluation;
    const auto call = snapshot ? graph.ops.size() - 4 : graph.ops.size() - 3;
    forged.operations[kProperty.value] = {EvaluationKind::kInline, graph.ops[call].first_use, {}};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    for (const auto& function : functions) {
      forged = evaluation;
      forged.operations[function.creation.value] = {EvaluationKind::kInline,
          UseId{graph.ops[call].first_use.value + 2}, {}}; // callee, receiver, callback.
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  } else {
    auto malformed = graph;
    const auto consumer = *evaluation.operations[kProperty.value].consumer;
    const auto call = std::get<OpId>(graph.uses[consumer.value].owner);
    if (pure_result) {
      // The call retains its original exceptional execution site. Only its
      // legal getter/callback subtree fuses; the pure result is still separate.
      test::Check(evaluation.operations[call.value].kind == EvaluationKind::kRoot);
      const auto& wrapper = graph.ops[call.value + 1];
      test::Check(std::holds_alternative<ValueUnary>(wrapper.operation));
      auto forged = evaluation;
      forged.operations[call.value] = {EvaluationKind::kInline, wrapper.first_use, {}};
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
    malformed.ops[call.value].use_count = 0;
    test::Check(std::get<EvaluationFailure>(VerifyPhaseExpressions(malformed, evaluation, {})) ==
        EvaluationError::kInvalidGraph);
    auto forged = evaluation;
    forged.operations[kProperty.value].consumer = graph.ops[kProperty.value].first_use;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation;
    forged.operations[kProperty.value].consumer = UseId{static_cast<std::uint32_t>(graph.uses.size())};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
  }
  // Include the planner's bounded retry, not just independent verification.
  return evaluation.work + std::get<PhaseEvaluationCost>(VerifyPhaseExpressions(graph, evaluation, {})).work;
}
}  // namespace
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    Run(1, profile, Corridor::kLexical);
    Run(1, profile, Corridor::kLexicalGuard);
    Run(1, profile, Corridor::kLexicalWrite);
    std::uint64_t previous_lexical = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(count, profile, Corridor::kLexicalGuard);
      if (count == 402) test::Check(work < 3 * previous_lexical);
      previous_lexical = work;
    }
    std::uint32_t prior_count = 0;
    std::uint64_t prior_work = 0, prior_wrapped_work = 0, prior_retry_work = 0;
    constexpr std::uint32_t kScalingStart = 200;
    for (const auto count : {1U, kScalingStart, 201U, 402U, 1608U}) {
      const auto work = Run(count, profile);
      const auto wrapped_work = Run(count, profile, Corridor::kTypeofArgument);
      Run(count, profile, Corridor::kClosure);
      Run(count, profile, Corridor::kPureResult);
      const auto retry_work = Run(count, profile, Corridor::kClosurePureResult);
      if (prior_count >= kScalingStart) {
        test::Check(work <= prior_work * (count / prior_count + 1));
        test::Check(wrapped_work <= prior_wrapped_work * (count / prior_count + 1));
        test::Check(retry_work <= prior_retry_work * (count / prior_count + 1));
      }
      prior_count = count; prior_work = work; prior_wrapped_work = wrapped_work;
      prior_retry_work = retry_work;
    }
    Run(2, profile, Corridor::kPure, true);
    for (const auto count : {1U, 200U, 201U, 402U}) {
      Run(count, profile, Corridor::kPure, false, true);
      Run(count, profile, Corridor::kPure, true, true);
    }
    for (const auto corridor : {Corridor::kWrongReceiver, Corridor::kDuplicate, Corridor::kEffect,
        Corridor::kChangedCatch, Corridor::kStableCatch, Corridor::kHandlerChange})
      Run(2, profile, corridor, false, true);
    for (const auto corridor : {Corridor::kEffect, Corridor::kJump, Corridor::kBranch,
        Corridor::kChangedCatch, Corridor::kStableCatch, Corridor::kWrongReceiver, Corridor::kDuplicate,
        Corridor::kArgument, Corridor::kArguments, Corridor::kReversedArguments, Corridor::kHandlerChange,
        Corridor::kTypeofArgument, Corridor::kClosure, Corridor::kClosureEnvironment, Corridor::kClosureHandler,
        Corridor::kLiteral, Corridor::kNestedLiteral, Corridor::kLiteralArguments, Corridor::kLiteralReversed,
        Corridor::kLiteralHandler})
      Run(2, profile, corridor);
    for (const auto corridor : {Corridor::kLiteral, Corridor::kNestedLiteral, Corridor::kLiteralArguments}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
        const auto work = Run(count, profile, corridor);
        if (count > 201) test::Check(work < 3 * previous);
        previous = work;
      }
    }
  }
}
