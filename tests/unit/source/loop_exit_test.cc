#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/control_skeleton.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::source;
void TerminalBoundaries() {
  // 0->1->0 is the natural loop. Its normal port 2 is not a return;
  // 1->3->4 is a private abrupt arm unless 4 has another entry/predecessor.
  for (const unsigned boundary : {0U, 1U, 2U}) {
    const std::vector<ControlVertex> nodes{
        {{0}, 2}, {{2}, 2}, {{4}, 0}, {{4}, 1},
        {{5}, 0, ControlTermination::kAbrupt}, {{5}, static_cast<std::uint8_t>(boundary == 2)}};
    std::vector<ControlLink> arcs{
        {{0}, {1}, analysis::FlowEdgeKind::kTaken, 0},
        {{0}, {2}, analysis::FlowEdgeKind::kFallthrough, 1},
        {{1}, {0}, analysis::FlowEdgeKind::kTaken, 0},
        {{1}, {3}, analysis::FlowEdgeKind::kFallthrough, 1},
        {{3}, {4}, analysis::FlowEdgeKind::kTaken, 0}};
    if (boundary == 2) arcs.push_back({{5}, {4}, analysis::FlowEdgeKind::kTaken, 0});
    std::vector<ControlNodeId> roots{{0}, {5}};
    if (boundary == 1) roots.push_back({4});
    ControlLoopLayout loops;
    loops.loops.push_back({{0}, {}});
    loops.owners = {LoopId{0}, LoopId{0}, {}, {}, {}, {}};
    ControlQuota quota{{}};
    ExtendLoopExitCorridors(ControlGraphView{ControlProjection{{}, {}, nodes, arcs}}, roots, loops, quota);
    test::Check(!loops.owners[2]);
    test::Check(loops.owners[3] == LoopId{0});
    test::Check(loops.owners[4] == (boundary == 0 ? std::optional<LoopId>{LoopId{0}} : std::nullopt));
  }
}
std::string EarlyTerminal(abc::ProfileId profile, bool raised) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  const auto extra = static_cast<std::uint8_t>(raised);
  // while (condition()) { const value = callback(); if (value) return/throw value; }
  // return payload; Both exit corridors are private, but only one is fallthrough.
  std::vector<std::uint8_t> code{
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), static_cast<std::uint8_t>(11 + extra),
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), static_cast<std::uint8_t>(3 + extra)};
  if (raised) code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
  else code.push_back(op(abc::Opcode::kRETURN));
  code.insert(code.end(), {op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-13 - extra),
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kRETURN)});
  const auto body = test::Body(code);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 3, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(
      identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto built = BuildControlPlan(graph);
  test::Check(std::holds_alternative<ControlPlan>(built));
  const auto& control = std::get<ControlPlan>(built);
  test::Check(control.loops.size() == 1);
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"}; syntax.parameters.resize(frame.parameters);
  const std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, 0, 1, TextId{0}};
  const auto constructed = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(constructed));
  const auto& tree = std::get<ConstructedFunction>(constructed).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source)));
  return source.str();
}
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
  TerminalBoundaries();
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const functions = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool carried : {false, true}) for (const bool stop : {false, true}) {
      const auto source = OuterTransfer(profile, stop, carried);
      if (emit) std::cout << source << ",\n";
    }
  }
  if (emit) std::cout << "];\n";
  if (emit) std::cout << "export const terminals = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool raised : {false, true}) {
      const auto source = EarlyTerminal(profile, raised);
      if (emit) std::cout << source << ",\n";
    }
  }
  if (emit) std::cout << "];\n";
}
