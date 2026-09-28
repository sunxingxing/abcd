#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::source;
std::string OuterTransfer(abc::ProfileId profile, bool stop, bool carried) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  // outer: while (nextOuter()) { while (nextInner()) {
  //   if (skip()) continue/break outer; body(); } after(); }
  // The inner loop has a normal exit at PC24 and an abrupt exit to PC0/30.
  const auto body = carried ? test::Body({
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 32,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 17,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJNEZ_IMM8), static_cast<std::uint8_t>(stop ? 20 : -16),
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-19),
      op(abc::Opcode::kLDA_V8), 6, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-34),
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)}) : test::Body({
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 26,
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 14,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJNEZ_IMM8), static_cast<std::uint8_t>(stop ? 14 : -16),
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-16),
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-28),
      op(abc::Opcode::kRETURNUNDEFINED)});
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(carried ? 1 : 0, carried ? 6 : 5, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(
      identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto built = BuildControlPlan(graph);
  if (const auto* error = std::get_if<ControlError>(&built))
    std::cerr << "outer transfer control error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ControlPlan>(built));
  const auto& control = std::get<ControlPlan>(built);
  test::Check(control.loops.size() == 2);
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"};
  syntax.parameters.resize(frame.parameters);
  const std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, 0, 1, TextId{0}};
  auto tree_result = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  if (const auto* error = std::get_if<FunctionTreeError>(&tree_result))
    std::cerr << "outer transfer tree error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ConstructedFunction>(tree_result));
  const auto& tree = std::get<ConstructedFunction>(tree_result).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(tree.labels.size() == 1);
  bool labelled = false;
  for (std::size_t i = 0; i < tree.statements.size(); ++i) {
    const auto* continued = std::get_if<ContinueStmt>(&tree.statements[i].statement);
    const auto* broken = std::get_if<BreakStmt>(&tree.statements[i].statement);
    if (!(continued && continued->label) && !(broken && broken->label)) continue;
    labelled = true;
    auto missing = tree;
    if (continued) std::get<ContinueStmt>(missing.statements[i].statement).label.reset();
    else std::get<BreakStmt>(missing.statements[i].statement).label.reset();
    test::Check(std::get<TreeStructureError>(VerifyTreeStructure(missing)) == TreeStructureError::kTarget);
    std::ostringstream refused;
    test::Check(std::holds_alternative<TreePrintError>(PrintFunctionTree(missing, refused)) && refused.str().empty());
  }
  test::Check(labelled);
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source)));
  return source.str();
}
}
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const functions = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool carried : {false, true}) for (const bool stop : {false, true}) {
      const auto source = OuterTransfer(profile, stop, carried);
      if (emit) std::cout << source << ",\n";
    }
  }
  if (emit) std::cout << "];\n";
}
