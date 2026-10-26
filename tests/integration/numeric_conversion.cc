#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
abc::MethodBody Body(unsigned count, bool legacy, bool phased = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  constexpr unsigned kByteBits = 8;
  constexpr auto kLegacy = static_cast<std::uint16_t>(abc::Opcode::kDEPRECATED_TONUMERIC_PREF_V8);
  for (unsigned i = 0; i < count; ++i) {
    if (legacy) bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 9, 0, 0, 0,
        static_cast<std::uint8_t>(kLegacy), static_cast<std::uint8_t>(kLegacy >> kByteBits), 0});
    else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kTONUMERIC_IMM8), 0});
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  if (phased) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  if (phased) {
    abc::TryRegion region; region.length = catch_pc - 1; region.handler_count = 1; body.tries.push_back(region);
    abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = static_cast<std::uint32_t>(bytes.size()) - catch_pc;
    body.handlers.push_back(handler);
  }
  return body;
}
std::uint64_t Run(abc::ProfileId profile, unsigned count, bool print = false, bool legacy = false) {
  const auto body = Body(count, legacy);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  for (const auto& operation : graph.ops) if (const auto* unary = std::get_if<ValueUnary>(&operation.operation);
      unary && unary->op == UnaryOperator::kToNumeric) {
    test::Check(operation.result && graph.values[operation.result->value].domain == ValueKinds::Defined());
  }
  if (count == 1) {
    const auto private_body = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDHOLE),
        static_cast<std::uint8_t>(abc::Opcode::kTONUMERIC_IMM8), 0,
        static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
    test::Check(!std::holds_alternative<FunctionInput>(FunctionInput::Build(identity, {300}, {100},
        private_body, frame, InputCaptureMode::kClosed)));
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, syntax.names.size()};
  if (count == 1) {
    auto strict_contract = syntax; strict_contract.identity.contract = static_cast<SourceContract>(0);
    test::Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, strict_contract, resolved)));
  }
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  if (count == 1) {
    auto strict_contract = tree; strict_contract.identity.contract = static_cast<SourceContract>(0);
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, strict_contract, resolved)));
    bool checked = false;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      if (!std::holds_alternative<UpdateExpr>(tree.expressions[i].expression)) continue;
      checked = true; auto changed = tree;
      std::get<UpdateExpr>(changed.expressions[i].expression).result = UpdateResult::kPrefix;
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, resolved)));
      changed = tree;
      const auto symbol = std::get<UpdateExpr>(changed.expressions[i].expression).symbol;
      std::get<UpdateExpr>(changed.expressions[i].expression).symbol = SymbolId{0};
      for (auto& node : changed.expressions) if (auto* save = std::get_if<AssignmentExpr>(&node.expression))
        if (auto* target = std::get_if<SymbolTarget>(&save->target); target && target->symbol == symbol) target->symbol = SymbolId{0};
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, changed, resolved)));
      break;
    }
    test::Check(checked);
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
void Phased(abc::ProfileId profile, unsigned count, bool legacy, bool print = false) {
  const auto body = Body(count, legacy, true);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
  IdfQuota work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin) &&
      std::holds_alternative<abc::RegisterId>(ssa.Values()[i].slot)) entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  const auto environments = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries, work, {}, PendingExceptionValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value"}; syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size()); const ExpressionConstants resolved{constants, syntax.names.size()};
  const ExceptionTreeInput context{body, input, regions, control};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));
  if (print) { std::ostringstream out; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, out))); std::cout << out.str(); }
}

}
int main(int argc, char** argv) {
  if (argc == 3) {
    const auto option = std::string_view(argv[1]);
    test::Check(option == "--source" || option == "--legacy-source" || option == "--phased-source" || option == "--phased-legacy-source");
    const auto count = static_cast<unsigned>(std::stoul(argv[2])); test::Check(count > 0 && count <= 402);
    if (option == "--phased-source" || option == "--phased-legacy-source")
      Phased(abc::ProfileId::kDynamic1206, count, option == "--phased-legacy-source", true);
    else Run(abc::ProfileId::kDynamic1206, count, true, option == "--legacy-source");
    return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool legacy : {false, true}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, count, false, legacy);
        Phased(profile, count, legacy);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
}
