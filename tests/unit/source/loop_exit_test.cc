#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/control_skeleton.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include "analysis/shared_exit_fixture.h"

#include <sstream>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::source;
void PreservingContinuation() {
  enum class Placement { kHeader, kLatch, kBoth };
  for (const auto placement : {Placement::kHeader, Placement::kLatch, Placement::kBoth})
    for (const std::uint32_t length : {1U, 200U, 201U, 402U}) {
    // Header 0 can return via 2; the post-test 1 exits via 3 or follows a
    // private phased/jump corridor back to 0. Either terminal can preserve RETURN;
    // neither arc order nor header/latch shape identifies that semantic role.
    std::vector<ControlVertex> nodes(length + 4);
    std::vector<ControlLink> arcs;
    ControlLoopLayout loops; loops.loops.push_back({{0}, {}});
    loops.owners.resize(nodes.size(), LoopId{0});
    loops.owners[2].reset(); loops.owners[3].reset();
    for (std::uint32_t b = 0; b < nodes.size(); ++b) {
      auto& node = nodes[b]; node.first_arc = {static_cast<std::uint32_t>(arcs.size())};
      const auto add = [&](std::uint32_t target) {
        const auto slot = node.arc_count++;
        arcs.push_back({{b}, {target}, slot ? analysis::FlowEdgeKind::kFallthrough : analysis::FlowEdgeKind::kTaken, slot});
      };
      if (b == 0) { add(1); add(2); }
      else if (b == 1) { add(3); add(4); }
      else if (b < 4) {
        node.termination = ControlTermination::kAbrupt;
        if ((b == 2 && placement != Placement::kLatch) ||
            (b == 3 && placement != Placement::kHeader))
          node.completion = ControlCompletion::kPreserving;
      }
      else add(b + 1 == nodes.size() ? 0 : b + 1);
    }
    constexpr ControlNodeId root{0}; ControlQuota quota{{}};
    ExtendLoopExitCorridors(ControlGraphView{ControlProjection{{}, {}, nodes, arcs}}, {&root, 1}, loops, quota);
    test::Check(loops.owners[2].has_value() == (placement == Placement::kLatch));
    test::Check(loops.owners[3].has_value() == (placement == Placement::kHeader));
  }
}
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
void SharedExitCorridor() {
  std::optional<std::uint64_t> work_400;
  for (const unsigned boundary : {0U, 1U, 2U})
    for (const std::uint32_t length : {1U, 200U, 201U, 400U, 800U}) {
    // Header 0 leaves to 5; two body branches leave through the shared arm
    // 4 -> corridor -> 5. Multiple predecessors are internal to this loop,
    // unless 4 is an independent root or has an external incoming edge.
    std::vector<ControlVertex> nodes(length + 6 + (boundary == 2));
    std::vector<ControlLink> arcs;
    ControlLoopLayout loops; loops.loops.push_back({{0}, {}});
    loops.owners.resize(nodes.size());
    for (unsigned i = 0; i < 4; ++i) loops.owners[i] = LoopId{0};
    for (std::uint32_t b = 0; b < nodes.size(); ++b) {
      auto& node = nodes[b]; node.first_arc = {static_cast<std::uint32_t>(arcs.size())};
      const auto add = [&](std::uint32_t target) {
        const auto slot = node.arc_count++;
        arcs.push_back({{b}, {target}, slot ? analysis::FlowEdgeKind::kFallthrough : analysis::FlowEdgeKind::kTaken, slot});
      };
      if (b == 0) { add(1); add(5); }
      else if (b == 1) { add(2); add(3); }
      else if (b == 2 || b == 3) { add(0); add(4); }
      else if (b == 4) add(6);
      else if (b == 5) node.termination = ControlTermination::kAbrupt;
      else if (boundary == 2 && b + 1 == nodes.size()) add(4);
      else if (b + 1 == length + 6) add(5);
      else add(b + 1);
    }
    std::vector<ControlNodeId> roots{{0}};
    if (boundary == 1) roots.push_back({4});
    // An external predecessor must remain a separate reachable island.
    if (boundary == 2) roots.push_back({static_cast<std::uint32_t>(nodes.size() - 1)});
    const auto original = arcs;
    const ControlGraphView view{ControlProjection{{}, {}, nodes, arcs}};
    ControlQuota quota{{}};
    const auto before = quota.RemainingWork();
    ExtendLoopExitCorridors(view, roots, loops, quota);
    const auto work = before - quota.RemainingWork();
    if (length == 400) work_400 = work;
    if (length == 800) test::Check(work_400 && work <= 3 * *work_400);
    test::Check(loops.owners[4].has_value() == (boundary == 0));
    test::Check(!loops.owners[5]);
    test::Check(arcs.size() == original.size());
    for (std::size_t e = 0; e < arcs.size(); ++e)
      test::Check(arcs[e].source == original[e].source && arcs[e].target == original[e].target);
    if (boundary == 0) {
      for (std::size_t b = 6; b < nodes.size(); ++b) test::Check(loops.owners[b] == LoopId{0});
      std::vector<ScopedControlEntry> entries{{{0}, {}}};
      const auto plan = BuildLoopControl(view, entries, std::move(loops), quota);
      VerifyControlRegions(view, entries, plan, quota);
      test::Check(plan.loops.size() == 1 && plan.loops.front().exit == analysis::WorkBlockId{5});
      auto changed = plan;
      changed.nodes[4].next = ControlId{5}; // Skip the shared corridor, with valid IDs.
      bool rejected = false;
      try { VerifyControlRegions(view, entries, changed, quota); }
      catch (ControlError error) { rejected = error == ControlError::kInvalidGraph; }
      test::Check(rejected);
    }
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
std::string StructuredSource(abc::ProfileId profile, const abc::MethodBody& body,
    std::uint32_t parameters, std::size_t loops) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, parameters, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(
      identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  test::Check(control.loops.size() == loops);
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"}; syntax.parameters.resize(frame.parameters);
  const std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants resolved{constants, 0, 1, TextId{0}};
  const auto constructed = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, resolved));
  test::Check(constructed.tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
      graph, control, evaluation, constructed.tree, resolved)));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(constructed.tree, source)));
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
  if (const auto* error = std::get_if<FunctionTreeFailure>(&tree_result))
    std::cerr << "outer transfer tree error " << static_cast<unsigned>(error->kind) << '\n';
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
    test::Check(std::holds_alternative<TreePrintFailure>(PrintFunctionTree(missing, refused)) && refused.str().empty());
  }
  test::Check(labelled);
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source)));
  return source.str();
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--shared-decision") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const std::uint32_t count : {2U, 200U, 201U, 400U}) for (const bool select_false : {false, true})
        std::cout << StructuredSource(profile, test::SharedDecisionBody(count, select_false), 4, 0) << ",\n";
    std::cout << "];\n"; return 0;
  }
  PreservingContinuation();
  TerminalBoundaries();
  SharedExitCorridor();
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
  if (emit) std::cout << "export const shared = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    const auto source = StructuredSource(profile, test::SharedExitBody(), 7, 1);
    if (emit) std::cout << source << ",\n";
  }
  if (emit) std::cout << "];\n";
}
