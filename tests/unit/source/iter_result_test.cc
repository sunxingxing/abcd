#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_edits.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "source/protocol_values.h"
#include "abcd/analysis/value_import.h"

#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Composition { kRoot, kEmptyObject, kBufferedObject, kCall, kShared };
std::uint8_t Op(abc::Opcode code) { return static_cast<std::uint8_t>(code); }
abc::MethodBody Body(std::uint32_t count, bool computed, bool boolean, bool caught, Composition composition = Composition::kRoot) {
  const std::uint8_t parameter = composition == Composition::kRoot ? 2 : 3;
  std::vector<std::uint8_t> code;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (composition == Composition::kEmptyObject || composition == Composition::kShared)
      code.insert(code.end(), {Op(abc::Opcode::kCREATEEMPTYOBJECT), Op(abc::Opcode::kSTA_V8), 2});
    if (composition == Composition::kBufferedObject) code.insert(code.end(),
        {Op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0, Op(abc::Opcode::kSTA_V8), 2});
    if (composition == Composition::kCall) code.insert(code.end(),
        {Op(abc::Opcode::kLDA_V8), 5, Op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0,
         Op(abc::Opcode::kSTA_V8), 2});
    if (computed) code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), parameter,
        Op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, Op(abc::Opcode::kSTA_V8), 0,
        Op(abc::Opcode::kLDA_V8), parameter,
        Op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, Op(abc::Opcode::kSTA_V8), 1});
    else if (boolean) code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(parameter + 1),
        Op(abc::Opcode::kISTRUE), Op(abc::Opcode::kSTA_V8), 1});
    code.insert(code.end(), {Op(abc::Opcode::kCREATEITERRESULTOBJ_V8_V8),
        static_cast<std::uint8_t>(computed ? 0 : parameter),
        static_cast<std::uint8_t>(computed || boolean ? 1 : parameter + 1)});
    if (composition == Composition::kShared) code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), 0});
    if (composition == Composition::kEmptyObject || composition == Composition::kBufferedObject || composition == Composition::kShared)
      code.insert(code.end(), {Op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 2, 0, 2,
          Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(composition == Composition::kShared ? 0 : 2)});
    if (composition == Composition::kCall) code.insert(code.end(),
        {Op(abc::Opcode::kSTA_V8), 0, Op(abc::Opcode::kLDA_V8), 2,
         Op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 5, 0});
  }
  code.push_back(Op(abc::Opcode::kRETURN));
  const auto handler = static_cast<std::uint32_t>(code.size());
  if (caught) code.push_back(Op(abc::Opcode::kRETURN));
  auto body = test::Body(code);
  if (caught) {
    abc::TryRegion region; region.offset = {0}; region.length = handler; region.handler_count = 1;
    abc::CatchHandler catch_handler; catch_handler.offset = {handler}; catch_handler.length = 1;
    body.tries.push_back(region); body.handlers.push_back(catch_handler);
  }
  return body;
}
void Admission(abc::ProfileId profile) {
  const auto body = Body(1, false, false, false);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 2, {}));
  const auto semantic = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, frame.registers));
  test::Check(std::holds_alternative<IterResult>(semantic.instructions.front().operation));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, semantic, frame.registers, frame.locals));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, semantic, proof, frame));
  test::Check(!VerifyImportedValues(graph, semantic, proof, frame));
  auto changed = graph; changed.ops.front().operation = ValueObjectAllocation{};
  test::Check(VerifyImportedValues(changed, semantic, proof, frame).has_value());
  changed = graph; changed.ops.front().before_throw = LanguageEffect::kNone;
  test::Check(VerifyImportedValues(changed, semantic, proof, frame).has_value());
  changed = graph; changed.uses.front().role = OperandRole::kCondition;
  test::Check(VerifyImportedValues(changed, semantic, proof, frame).has_value());
  for (const auto operand : {0U, 1U}) {
    auto malformed = body.instructions.front();
    malformed.decoded.operands[operand].raw_bits = frame.registers;
    test::Check(std::get<SemanticFailure>(LiftSemanticInstruction(malformed, profile, frame.registers)).kind ==
        SemanticError::kMalformedInstruction);
  }
}
void Run(abc::ProfileId profile, bool phased, std::uint32_t count, bool computed,
         bool boolean, bool print = false, Composition composition = Composition::kRoot) {
  const auto body = Body(count, computed, boolean, phased, composition);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(composition == Composition::kRoot ? 2 : 3, composition == Composition::kCall ? 3 : 2, {}));
  ValueProgram graph;
  std::optional<ExceptionFlow> phases;
  std::optional<ExceptionSsa> ssa;
  std::optional<EnvironmentFlow> env;
  std::optional<ExceptionValueFlow> domains;
  std::optional<ExceptionInputProof> proof;
  std::optional<PhasedValueInput> input;
  if (phased) {
    phases = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
    IdfQuota work{kDefaultSemanticWork};
    ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(*phases, frame.registers, work));
    std::vector<EntryValueFact> entries;
    for (std::size_t i = 0; i < ssa->Values().size(); ++i) {
      const auto& value = ssa->Values()[i];
      const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
      if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
        entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
    }
    env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(*phases));
    domains = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(*phases, *ssa, entries,
        work, {}, PendingExceptionValues::kLanguage));
    proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(*phases, *ssa, *env, *domains));
    input.emplace(PhasedValueInput{*phases, *ssa, *env, *domains, *proof, frame});
    graph = std::get<ValueProgram>(ImportPhasedValues(*input));
    test::Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, *input)));
  } else {
    RouteInputIdentity identity; identity.profile = profile;
    const auto original = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body,
        frame, InputCaptureMode::kClosed));
    graph = std::get<FunctionValues>(BuildFunctionValues(original, body, frame)).graph;
  }
  std::vector<OpId> creations;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) if (std::holds_alternative<ValueIterResult>(graph.ops[i].operation)) {
    const auto& op = graph.ops[i]; creations.push_back({static_cast<std::uint32_t>(i)});
    test::Check(op.life == ValueLife::kLive && op.use_count == 2 && op.result &&
        op.normal == LanguageEffect::kAllocation && op.before_throw == op.normal &&
        op.exceptional == ExceptionalCompletion::kMayThrow &&
        graph.values[op.result->value].domain.IsLanguage());
    auto stripped = op; stripped.normal = stripped.before_throw = LanguageEffect::kNone;
    stripped.exceptional = ExceptionalCompletion::kImpossible;
    test::Check(!CanErasePureOp(stripped));
  }
  test::Check(creations.size() == count);
  const auto scheduled = phased ? BuildPhasedEvaluationPlan(graph) : BuildEvaluationPlan(graph);
  if (composition == Composition::kShared) {
    test::Check(std::holds_alternative<EvaluationFailure>(scheduled)); return;
  }
  test::Check(std::holds_alternative<EvaluationPlan>(scheduled));
  const auto evaluation = std::get<EvaluationPlan>(scheduled);
  if (composition == Composition::kEmptyObject || composition == Composition::kBufferedObject) {
    test::Check(evaluation.literals.has_value());
    for (const auto creation : creations) {
      test::Check(evaluation.literals->nested[creation.value].has_value());
      if (count <= 2) {
        auto wrong = evaluation; wrong.literals->nested[creation.value].reset();
        const auto checked = phased ? VerifyPhasedEvaluationPlan(graph, wrong) : VerifyEvaluationPlan(graph, wrong);
        test::Check(std::holds_alternative<EvaluationFailure>(checked));
      }
    }
  }
  FunctionSyntax syntax{}; syntax.identity.profile = profile; syntax.method = {100};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"done", u"input", u"flag", u"result", u"receiver"};
  syntax.parameters = {{NameId{2}, NameEvidence::kDebugRange}, {NameId{3}, NameEvidence::kDebugRange}};
  if (composition == Composition::kCall) syntax.parameters.push_back({NameId{5}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value == 2 ? 4U : read->name.value};
    if (std::holds_alternative<ValueOwnWrite>(graph.ops[i].operation)) constants[i] = NameId{4};
    if (const auto* buffer = std::get_if<ValueObjectAllocation>(&graph.ops[i].operation); buffer && buffer->literal)
      constants[i] = AggregateConstant{0, 1};
    if (std::holds_alternative<ValueIterResult>(graph.ops[i].operation)) constants[i] = IterResultConstant{{0}, {1}};
  }
  const std::vector<ConstantProperty> properties = composition == Composition::kBufferedObject ?
      std::vector<ConstantProperty>{{NameId{4}, NullLiteral{}}} : std::vector<ConstantProperty>{};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  FunctionTree tree;
  std::optional<ControlPlan> control;
  std::optional<ExceptionPlan> regions;
  std::optional<ExceptionControl> exception_control;
  std::optional<ExceptionTreeInput> context;
  if (phased) {
    regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, *input));
    exception_control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, *input, *regions));
    context.emplace(ExceptionTreeInput{body, *input, *regions, *exception_control});
    const auto built = BuildExceptionFunctionDraft(graph, *context, evaluation, syntax, resolved, {});
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
    tree = std::get<ExceptionFunctionDraft>(built).tree;
  } else {
    control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto built = BuildFunctionTree(graph, *control, evaluation, syntax, resolved);
    test::Check(std::holds_alternative<ConstructedFunction>(built));
    tree = std::get<ConstructedFunction>(built).tree;
  }
  const auto verify = [&](const FunctionTree& candidate) {
    return phased ? VerifyExceptionTreeSemantics(graph, *context, evaluation, candidate, resolved, {})
        : VerifyTreeSemantics(graph, *control, evaluation, candidate, resolved);
  };
  const auto semantics = verify(tree);
  test::Check(std::holds_alternative<TreeSemantics>(semantics));
  const auto& original = std::get<TreeSemantics>(semantics).flow.original;
  test::Check(original != nullptr);
  if (count <= 2) test::CheckProtocolValues(tree, *original, creations);
  test::Check(tree.properties.size() == count * (composition == Composition::kEmptyObject || composition == Composition::kBufferedObject ? 3 : 2) && tree.recovery_issues.empty());
  if (count <= 2) {
    auto wrong = tree; std::swap(wrong.properties[0], wrong.properties[1]);
    test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    wrong = tree; wrong.names[0] = u"different";
    test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    wrong = tree; wrong.properties[1].value = wrong.properties[0].value;
    test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    wrong = tree; wrong.properties[0].origin = TreeOriginId{static_cast<std::uint32_t>(tree.origins.size() - 1)};
    test::Check(std::holds_alternative<TreeSemanticError>(verify(wrong)));
    if (!phased && !computed && composition == Composition::kRoot) {
      char path[] = "/tmp/abcd-iter-constants-XXXXXX";
      const auto fd = mkstemp(path); test::Check(fd >= 0 && close(fd) == 0);
      const auto opened = corpus::AbcFile::Open(path);
      const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
      abc::Header header; header.profile = profile;
      const auto result = ResolveTreeConstants(file, header, {}, {100}, graph);
      test::Check(std::holds_alternative<TreeConstants>(result));
      const auto& actual = std::get<TreeConstants>(result);
      test::Check(actual.names == std::vector<std::u16string>{u"value", u"done"});
      for (const auto creation : creations) {
        const auto keys = std::get<IterResultConstant>(actual.operations[creation.value]);
        test::Check(keys.value == NameId{0} && keys.done == NameId{1});
      }
      test::Check(unlink(path) == 0);
    }
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, nullptr, original.get())));
  if (boolean) test::Check(output.str().find("=== true") == std::string::npos);
  if (print) std::cout << output.str();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    if (option == "--nested-source" || option == "--nested-buffer-source" || option == "--call-source") {
      Run(abc::ProfileId::kDynamic1206, true, 1, true, false, true, option == "--call-source" ? Composition::kCall :
          option == "--nested-buffer-source" ? Composition::kBufferedObject : Composition::kEmptyObject); return 0;
    }
    const bool caught = option == "--caught-source";
    const bool computed = caught || option == "--computed-source";
    const bool boolean = option == "--boolean-source";
    test::Check(caught || computed || boolean || option == "--source");
    Run(abc::ProfileId::kDynamic1206, caught, 1, computed, boolean, true); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    Admission(profile);
    for (const auto count : {1U, 2U, 200U, 201U, 402U})
      for (const bool phased : {false, true}) {
        Run(profile, phased, count, false, false);
        Run(profile, phased, count, true, false);
        Run(profile, phased, count, false, true);
        for (const auto composition : {Composition::kEmptyObject, Composition::kBufferedObject, Composition::kCall}) {
          Run(profile, phased, count, true, false, false, composition);
          Run(profile, phased, count, false, true, false, composition);
        }
        if (count <= 2) Run(profile, phased, count, false, false, false, Composition::kShared);
      }
  }
}
