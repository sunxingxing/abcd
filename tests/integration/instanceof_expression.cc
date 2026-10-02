#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
std::uint64_t Run(abc::ProfileId profile, unsigned count, bool print = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kINSTANCEOF_IMM8_V8), 0, 0});
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 2, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"left", u"right"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}, {NameId{1}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  if (count == 1) {
    bool checked = false;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      if (!std::holds_alternative<BinaryExpr>(tree.expressions[i].expression)) continue;
      checked = true; auto forged = tree;
      auto& binary = std::get<BinaryExpr>(forged.expressions[i].expression);
      std::swap(binary.left, binary.right);
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, forged, resolved)));
      forged = tree; std::get<BinaryExpr>(forged.expressions[i].expression).operation = BinaryOperator::kIn;
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, forged, resolved)));
      break;
    }
    test::Check(checked);
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
}
int main(int argc, char** argv) {
  if (argc == 2) {
    test::Check(std::string_view(argv[1]) == "--source");
    Run(abc::ProfileId::kDynamic1206, 1, true); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(profile, count);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
