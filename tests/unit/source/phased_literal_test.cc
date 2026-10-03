#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/exception_locals.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Shape {
  kStaged, kStagedObserve, kStagedCatchObserve, kReturn, kCatch, kObserve, kLate, kBranch, kCatchObserve, kNested, kNestedCatch,
  kNestedCatchObserve, kNestedDispatch, kNestedShared, kNestedSnapshot, kUnary, kUnaryCatch,
  kUnaryShared, kUnaryUnknown, kNestedDeadCopy, kNestedDeadArray, kGlobalCatch, kGlobalDispatch, kLexicalCatch, kBinaryCatch
};
std::uint64_t Run(abc::ProfileId profile, Shape shape = Shape::kReturn,
                  std::uint32_t copies = 0, std::uint32_t fields = 1,
                  abc::Opcode define = abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8,
                  abc::Opcode unary = abc::Opcode::kNEG_IMM8, bool print = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kObject = 0, kParameter = 4;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), kObject};
  const bool staged = shape == Shape::kStaged || shape == Shape::kStagedObserve || shape == Shape::kStagedCatchObserve;
  if (staged) bytes = {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0,
      op(abc::Opcode::kSTA_V8), kObject};
  const bool binary = shape == Shape::kBinaryCatch;
  const bool lexical = shape == Shape::kLexicalCatch;
  std::uint32_t lexical_try_begin = 0;
  if (lexical) {
    const std::vector<std::uint8_t> prelude{op(abc::Opcode::kNEWLEXENV_IMM8), 1,
        op(abc::Opcode::kLDA_V8), kParameter + 1, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0};
    lexical_try_begin = static_cast<std::uint32_t>(prelude.size());
    bytes.insert(bytes.begin(), prelude.begin(), prelude.end());
  }
  const bool dead_copy = shape == Shape::kNestedDeadCopy || shape == Shape::kNestedDeadArray;
  const bool global = shape == Shape::kGlobalCatch || shape == Shape::kGlobalDispatch;
  const bool nested = shape == Shape::kNested || shape == Shape::kNestedCatch ||
      shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedShared || shape == Shape::kNestedSnapshot ||
      dead_copy || global || lexical;
  const bool arithmetic = shape == Shape::kUnary || shape == Shape::kUnaryCatch ||
      shape == Shape::kUnaryShared || shape == Shape::kUnaryUnknown;
  const bool numeric_update = unary == abc::Opcode::kTONUMERIC_IMM8 ||
      unary == abc::Opcode::kINC_IMM8 || unary == abc::Opcode::kDEC_IMM8;
  const bool caught = staged || shape == Shape::kCatch || shape == Shape::kCatchObserve ||
      shape == Shape::kNestedCatch || shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedSnapshot || shape == Shape::kUnaryCatch ||
      dead_copy || global || lexical || binary;
  constexpr std::uint8_t kSaved = 2, kSnapshotInput = 6, kSnapshotPrelude = 4;
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.begin(),
      {op(abc::Opcode::kLDA_V8), kSnapshotInput, op(abc::Opcode::kSTA_V8), kSaved});
  const auto initialized_pc = static_cast<std::uint32_t>(bytes.size());
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.end(),
      {op(abc::Opcode::kLDTRUE), op(abc::Opcode::kSTA_V8), kSaved});
  for (std::uint32_t i = 0; i < (dead_copy ? 0 : copies); ++i)
    bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kObject, kObject});
  if (shape == Shape::kObserve || shape == Shape::kStagedObserve) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  if (staged) bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kJEQZ_IMM8), 10,
      op(abc::Opcode::kLDA_V8), kParameter, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0,
      op(abc::Opcode::kJMP_IMM8), 3, op(abc::Opcode::kLDNULL)});
  if (shape == Shape::kBranch) bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 2});
  if (!nested && !arithmetic && !staged) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
  if (shape == Shape::kLate) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  for (std::uint32_t i = 0; i < fields; ++i) {
    constexpr std::uint8_t kInner = 1;
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), kInner});
    if (dead_copy) {
      if (shape == Shape::kNestedDeadArray)
        bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0, op(abc::Opcode::kSTA_V8), kSaved});
      else bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kSaved, kInner});
      for (std::uint32_t c = 0; c < copies; ++c)
        bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), kSaved, kSaved});
    }
    if (lexical) {
      constexpr auto kGuard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
      constexpr unsigned kByteBits = 8;
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0,
          static_cast<std::uint8_t>(kGuard), static_cast<std::uint8_t>(kGuard >> kByteBits), 0, 0});
    } else if (global) bytes.insert(bytes.end(), {op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 1, 0,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    else if (nested && shape != Shape::kNestedDeadArray) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),
        static_cast<std::uint8_t>(shape == Shape::kNestedSnapshot ? kSnapshotInput :
            dead_copy ? kParameter + 2 : kParameter + 1)});
    if (binary) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_STR_ID16), 0, 0,
        op(abc::Opcode::kADD2_IMM8_V8), 0, kParameter});
    if (arithmetic) {
      if (shape != Shape::kUnaryUnknown &&
          (unary == abc::Opcode::kNEG_IMM8 || unary == abc::Opcode::kTONUMBER_IMM8 || numeric_update))
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0});
      else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kParameter});
      bytes.push_back(op(unary));
      if (unary != abc::Opcode::kISTRUE && unary != abc::Opcode::kISFALSE) bytes.push_back(0);
    }
    bytes.insert(bytes.end(), {op(define), 0});
    if (define == abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8) bytes.push_back(0);
    bytes.insert(bytes.end(), {0, 0, nested ? kInner : kObject});
    if (shape == Shape::kUnaryShared) bytes.insert(bytes.end(),
        {op(define), 0, 0, 0, kObject});
    if (nested) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kInner,
        op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, kObject});
  }
  if (shape == Shape::kNestedShared) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, kObject});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject, op(abc::Opcode::kRETURN)});
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  if (shape == Shape::kStagedCatchObserve || shape == Shape::kCatchObserve || shape == Shape::kNestedCatchObserve) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject});
  if (shape == Shape::kNestedSnapshot) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kSaved});
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {shape == Shape::kStagedCatchObserve || shape == Shape::kCatchObserve || shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kGlobalDispatch ? initialized_pc : 0};
    if (shape == Shape::kNestedSnapshot) region.offset = {kSnapshotPrelude};
    if (lexical) region.offset = {lexical_try_begin};
    region.length = catch_pc - region.offset.value; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = bytes.size() - catch_pc;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      shape == Shape::kNestedSnapshot || dead_copy ? 3 : nested ? 2 : 1, 4, {true, true, false, true}));
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
  const auto inputs = VerifyExceptionInputs(flow, ssa, env, values);
  if (shape == Shape::kUnaryUnknown && unary != abc::Opcode::kTONUMERIC_IMM8 && unary != abc::Opcode::kTONUMBER_IMM8) {
    // NEG over arbitrary language values can produce an uncertified VM NaN.
    // Preserve the input proof refusal rather than admitting native syntax.
    test::Check(std::holds_alternative<ExceptionInputFailure>(inputs));
    return 0;
  }
  const auto proof = std::get<ExceptionInputProof>(inputs);
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto result = BuildPhasedEvaluationPlan(graph);
  if (shape == Shape::kStagedObserve || shape == Shape::kStagedCatchObserve || shape == Shape::kObserve || shape == Shape::kBranch || shape == Shape::kCatchObserve ||
      shape == Shape::kNestedCatchObserve || shape == Shape::kNestedDispatch || shape == Shape::kNestedShared || shape == Shape::kNestedSnapshot ||
      shape == Shape::kUnaryShared || shape == Shape::kGlobalDispatch) {
    test::Check(std::get<EvaluationError>(result) == EvaluationError::kUnsupportedOperation);
    return 0;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(result));
  const auto& evaluation = std::get<EvaluationPlan>(result);
  if (shape == Shape::kStaged) {
    test::Check(std::any_of(evaluation.operations.begin(), evaluation.operations.end(),
        [](const OpEvaluation& op) { return op.kind == EvaluationKind::kPhiInput; }));
    for (std::size_t i = 0; i < evaluation.operations.size(); ++i) {
      if (evaluation.operations[i].kind != EvaluationKind::kPhiInput) continue;
      test::Check(graph.ops[i].normal == LanguageEffect::kNone &&
          graph.ops[i].exceptional == ExceptionalCompletion::kImpossible);
      auto forged = evaluation;
      forged.operations[i].consumer = UseId{0};
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
  }
  test::Check(evaluation.literals && evaluation.literals->objects.size() == (nested ? fields + 1 : 1));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  const auto& literal = *evaluation.literals;
  if (lexical) {
    test::Check(evaluation.cells.has_value());
    bool checked = false;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (!std::holds_alternative<ValueLexicalLoad>(graph.ops[i].operation)) continue;
      checked = true;
      auto invalid = evaluation;
      invalid.cells->initialization.reads[i] = NotCellRead{};
      test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)));
      test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, invalid)));
      break;
    }
    test::Check(checked);
    auto invalid = evaluation;
    ++invalid.cells->initialization.cfg_revision.value;
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)));
    invalid = evaluation; invalid.cells.reset();
    test::Check(std::holds_alternative<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)));
  }
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
  if (dead_copy) {
    // A forged dead label cannot hide an own-store effect or a copy whose
    // result still reaches the outer property, even with an identity alias map.
    auto invalid = evaluation;
    invalid.operations[literal.definitions.front().value] = {EvaluationKind::kElidedPure};
    test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)) == LiteralError::kFlow);
    const auto definition = literal.definitions.front();
    const auto rhs = std::get<ValueId>(graph.uses[graph.ops[definition.value].first_use.value + 1].operand);
    const auto producer = std::get<OpResult>(graph.values[rhs.value].origin).op;
    test::Check(std::holds_alternative<ValueCopy>(graph.ops[producer.value].operation));
    invalid = evaluation; invalid.operations[producer.value] = {EvaluationKind::kElidedPure};
    invalid.aliases[rhs.value] = rhs;
    test::Check(std::get<LiteralError>(VerifyPhasedLiteralPlan(graph, literal, invalid)) == LiteralError::kFlow);
  }
  if (staged || shape == Shape::kCatch || shape == Shape::kNestedCatch || shape == Shape::kUnaryCatch || dead_copy || shape == Shape::kGlobalCatch || lexical || binary ||
      (shape == Shape::kUnaryUnknown && (unary == abc::Opcode::kTONUMERIC_IMM8 || unary == abc::Opcode::kTONUMBER_IMM8))) {
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(1); syntax.names = {u"__proto__", u"Lookup", u"value"};
    syntax.texts = {u"suffix"};
    std::vector<ExpressionConstant> constants(graph.ops.size());
    for (const auto id : literal.definitions) constants[id.value] = NameId{0};
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
      if (std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation)) constants[i] = NameId{1};
      if ((global || staged) && std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{2};
    }
    std::vector<ConstantProperty> properties;
    if (staged) {
      test::Check(literal.objects.front().form == LiteralForm::kStatements);
      constants[literal.objects.front().allocation.value] = AggregateConstant{0, 1};
      properties.push_back({NameId{0}, NullLiteral{}});
    }
    const ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size(), {}, properties};
    const ExceptionTreeInput context{body, input, regions, control};
    // This exercises repeated independent tree/phase checks as well as the
    // initializer proof. Keep its explicit allowance separate from the exact
    // initializer-work tests above; production defaults are unchanged.
    constexpr SemanticBudget kTreeBudget{};
    const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved, kTreeBudget);
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
    const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
    test::Check(tree.properties.size() == (staged ? 1 : nested ? fields * 2 : fields));
    test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
        graph, context, evaluation, tree, resolved, kTreeBudget)));
    if (print) {
      const GlobalScope globals{};
      std::ostringstream output;
      test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &globals)));
      std::cout << output.str();
    }
  }
  return literal.work;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const auto option = std::string_view(argv[1]);
    const bool update = option == "--increment-source" || option == "--decrement-source" || option == "--numeric-source";
    if (option == "--staged-source") {
      Run(abc::ProfileId::kDynamic1206, Shape::kStaged, 0, 2,
          abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abc::Opcode::kNEG_IMM8, true);
      return 0;
    }
    test::Check(option == "--global-source" || option == "--lexical-source" || update);
    const auto unary = option == "--increment-source" ? abc::Opcode::kINC_IMM8 :
        option == "--decrement-source" ? abc::Opcode::kDEC_IMM8 :
        option == "--numeric-source" ? abc::Opcode::kTONUMERIC_IMM8 : abc::Opcode::kNEG_IMM8;
    Run(abc::ProfileId::kDynamic1206, update ? Shape::kUnaryCatch :
        option == "--lexical-source" ? Shape::kLexicalCatch : Shape::kGlobalCatch, 0, 2,
        abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary, true);
    return 0;
  }
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    for (const auto shape : {Shape::kStaged, Shape::kStagedObserve, Shape::kStagedCatchObserve, Shape::kReturn, Shape::kCatch, Shape::kObserve, Shape::kLate, Shape::kBranch,
                             Shape::kCatchObserve, Shape::kNested, Shape::kNestedCatch, Shape::kNestedCatchObserve,
                             Shape::kNestedSnapshot, Shape::kNestedDispatch, Shape::kNestedShared,
                             Shape::kUnary, Shape::kUnaryCatch, Shape::kUnaryShared, Shape::kUnaryUnknown,
                             Shape::kNestedDeadCopy, Shape::kNestedDeadArray, Shape::kGlobalCatch, Shape::kGlobalDispatch, Shape::kLexicalCatch, Shape::kBinaryCatch}) Run(profile, shape);
    for (const auto unary : {abcd::abc::Opcode::kNOT_IMM8, abcd::abc::Opcode::kTYPEOF_IMM8,
        abcd::abc::Opcode::kTONUMBER_IMM8, abcd::abc::Opcode::kISTRUE, abcd::abc::Opcode::kISFALSE,
        abcd::abc::Opcode::kTONUMERIC_IMM8, abcd::abc::Opcode::kINC_IMM8, abcd::abc::Opcode::kDEC_IMM8})
      Run(profile, Shape::kUnaryCatch, 0, 1, abcd::abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
    for (const auto unary : {abc::Opcode::kTONUMERIC_IMM8, abc::Opcode::kINC_IMM8, abc::Opcode::kDEC_IMM8}) {
      Run(profile, Shape::kUnaryUnknown, 0, 1, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
      Run(profile, Shape::kUnaryShared, 0, 1, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
      std::uint64_t previous = 0;
      for (const auto fields : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, Shape::kUnaryCatch, 0, fields, abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8, unary);
        if (fields == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
    for (const auto opcode : {abcd::abc::Opcode::kSTOWNBYNAME_IMM16_ID16_V8,
        abcd::abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8, abcd::abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8})
      Run(profile, Shape::kCatch, 0, 1, opcode);
    for (const auto copies : {1U, 200U, 201U, 402U, 804U}) Run(profile, Shape::kCatch, copies);
    for (const auto fields : {2U, 200U, 201U, 400U, 800U})
      Run(profile, Shape::kNestedCatch, 0, fields);
    for (const auto fields : {2U, 200U, 201U, 400U, 800U})
      Run(profile, Shape::kUnaryCatch, 0, fields);
    std::uint64_t previous_copies = 0, previous_arrays = 0;
    for (const auto count : {2U, 200U, 201U, 400U, 800U}) {
      const auto copy_work = Run(profile, Shape::kNestedDeadCopy, count);
      Run(profile, Shape::kNestedDeadArray, count);
      const auto array_work = Run(profile, Shape::kNestedDeadArray, 1, count);
      if (count > 201) {
        test::Check(copy_work < 3 * previous_copies);
        test::Check(array_work < 3 * previous_arrays);
      }
      previous_copies = copy_work; previous_arrays = array_work;
    }
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      Run(profile, Shape::kBinaryCatch, 0, count);
      Run(profile, Shape::kStaged, 0, count);
    }
    std::uint64_t lexical_work = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, Shape::kLexicalCatch, 0, count);
      if (count == 402) test::Check(work < 3 * lexical_work);
      lexical_work = work;
    }
    std::uint64_t previous = 0;
    for (const auto fields : {200U, 201U, 402U, 804U}) {
      const auto work = Run(profile, Shape::kCatch, 0, fields);
      if (fields > 201) abcd::test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
