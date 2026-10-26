#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include <array>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
using Seed = std::variant<analysis::IntegerLiteral, analysis::Float64Bits>;
constexpr unsigned kByteBits = 8;
abc::MethodBody Body(Seed seed, unsigned count) {
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  std::vector<std::uint8_t> bytes;
  if (const auto* integer = std::get_if<analysis::IntegerLiteral>(&seed)) {
    bytes.push_back(op(abc::Opcode::kLDAI_IMM32));
    const auto bits = static_cast<std::uint32_t>(integer->value);
    for (unsigned shift = 0; shift < sizeof(bits) * kByteBits; shift += kByteBits)
      bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
  } else {
    bytes.push_back(op(abc::Opcode::kFLDAI_IMM64));
    const auto bits = std::get<analysis::Float64Bits>(seed).value;
    for (unsigned shift = 0; shift < sizeof(bits) * kByteBits; shift += kByteBits)
      bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
  }
  constexpr auto kNeg = static_cast<std::uint16_t>(abc::Opcode::kDEPRECATED_NEG_PREF_V8);
  for (unsigned i = 0; i < count; ++i) bytes.insert(bytes.end(), {
      op(abc::Opcode::kSTA_V8), 0, static_cast<std::uint8_t>(kNeg),
      static_cast<std::uint8_t>(kNeg >> kByteBits), 0});
  bytes.push_back(op(abc::Opcode::kRETURN));
  return test::Body(bytes);
}
std::uint64_t Run(abc::ProfileId profile, Seed seed, unsigned count, bool admitted, bool print = false) {
  const auto body = Body(seed, count);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
  RouteInputIdentity identity; identity.profile = profile;
  identity.contract = SourceContract::kLanguageAndHostV3;  // Pinned analysis fixture.
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  if (!admitted) { test::Check(!std::holds_alternative<FunctionInput>(prepared)); return 0; }
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity.contract = SourceContract::kLanguageAndHostV3; syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  std::ostringstream out; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, out)));
  if (print) std::cout << out.str();
  return evaluation.work;
}
void Phased(abc::ProfileId profile, Seed seed, bool admitted) {
  auto body = Body(seed, 2);
  const auto end = body.instructions.back().offset;
  auto handler_instruction = test::Body({static_cast<std::uint8_t>(abc::Opcode::kRETURN)}).instructions.front();
  handler_instruction.offset = {end.value + 1}; body.instructions.push_back(handler_instruction);
  abc::TryRegion region; region.length = end.value; region.handler_count = 1; body.tries.push_back(region);
  abc::CatchHandler handler; handler.offset = handler_instruction.offset; handler.length = 1; body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 0, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
  IdfQuota work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, work));
  const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, {}, work, {}, PendingExceptionValues::kLanguage));
  const auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
  if (!admitted) { test::Check(std::holds_alternative<ExceptionInputFailure>(checked)); return; }
  const PhasedValueInput input{flow, ssa, environments, values, std::get<ExceptionInputProof>(checked), frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  FunctionSyntax syntax{}; syntax.identity.contract = SourceContract::kLanguageAndHostV3; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants};
  const ExceptionTreeInput context{body, input, regions, control};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));
}

void MissingWitness() {
  // Isolated verifier boundary: an explicit input premise may be weakened,
  // but native legacy-minus meaning must not survive without its tag witness.
  ValueProgram graph;
  const auto number = ValueKinds::NonNaNNumber().WithoutInt32MinTag();
  graph.values = {{EntryDefinition{ParameterEntry{0}}, number, UseId{0}}, {OpResult{{0}}, number, UseId{1}}};
  graph.origins = {{{0}, {0}, {}}, {{1}, {3}, {}}};
  graph.ops = {{ValueUnary{UnaryOperator::kLegacyNegate}, {0}, 0, {0}, 1, ValueId{1},
      LanguageEffect::kUserCode, LanguageEffect::kUserCode, ExceptionalCompletion::kMayThrow, {0}},
      {ValueReturn{}, {0}, 1, {1}, 1, {}, LanguageEffect::kNone, LanguageEffect::kNone,
       ExceptionalCompletion::kImpossible, {1}}};
  graph.uses = {{OpId{0}, OperandRole::kInput, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}},
      {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}}};
  graph.blocks = {{{0}, 2, {0}, 0}};
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity.contract = SourceContract::kLanguageAndHostV3; syntax.owner = {0}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value"}; syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  graph.values[0].domain = ValueKinds::NonNaNNumber();
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, resolved)));
}

}
int main(int argc, char** argv) {
  constexpr std::array<Seed, 8> seeds{analysis::IntegerLiteral{0}, analysis::IntegerLiteral{7}, analysis::IntegerLiteral{-7}, analysis::IntegerLiteral{INT32_MAX},
      analysis::Float64Bits{UINT64_C(0xc1e0000000000000)}, analysis::Float64Bits{UINT64_C(0x8000000000000000)},
      analysis::Float64Bits{UINT64_C(0x7ff0000000000000)}, analysis::Float64Bits{UINT64_C(0xfff0000000000000)}};
  if (argc == 3) {
    test::Check(std::string_view(argv[1]) == "--source");
    const auto index = std::stoul(argv[2]); test::Check(index < seeds.size());
    Run(abc::ProfileId::kDynamic1206, seeds[index], 1, true, true); return 0;
  }
  MissingWitness();
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const auto seed : seeds) { Run(profile, seed, 1, true); Phased(profile, seed, true); }
    Run(profile, analysis::IntegerLiteral{INT32_MIN}, 1, false);
    Phased(profile, analysis::IntegerLiteral{INT32_MIN}, false);
    Run(profile, analysis::Float64Bits{UINT64_C(0x7fff000000000005)}, 1, false);
    Phased(profile, analysis::Float64Bits{UINT64_C(0x7fff000000000005)}, false);
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, analysis::IntegerLiteral{7}, count, true);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
