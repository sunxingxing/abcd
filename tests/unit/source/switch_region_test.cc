#include "../../../src/source/switch_region.h"

#include <iostream>

#include "../../../src/source/function_tree.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/switch_syntax.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_labels.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../support/analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
template <class T, class V>
T Take(V v, std::source_location at = std::source_location::current()) {
  if (!std::holds_alternative<T>(v)) {
    std::cerr << "at " << at.line() << " take " << typeid(T).name()
              << " variant " << v.index() << "\n";
    std::visit(
        [](const auto& x) {
          if constexpr (std::is_enum_v<std::decay_t<decltype(x)>>)
            std::cerr << unsigned(x) << "\n";
        },
        v);
    std::abort();
  }
  return std::get<T>(std::move(v));
}
analysis::ValueProgram Graph(unsigned count, bool copies = false,
                             bool split = false, unsigned width = 2,
                             bool nonadjacent = false) {
  std::vector<std::uint8_t> code;
  const auto byte = [&](unsigned n) {
    Check(n <= 255);
    code.push_back(n);
  };
  const auto op = [&](abc::Opcode x) { byte(static_cast<unsigned>(x)); };
  const auto jump = [&](abc::Opcode x) {
    const auto pc = code.size();
    op(x);
    byte(0);
    byte(0);
    return pc;
  };
  const auto bind = [&](std::size_t pc, std::size_t target) {
    Check(target >= pc && target - pc < 32768);
    const auto n = target - pc;
    code[pc + 1] = n;
    code[pc + 2] = n >> 8;
  };
  const auto number = [&](unsigned n) {
    op(abc::Opcode::kLDAI_IMM32);
    for (unsigned b = 0; b < 4; ++b) byte((n >> (8 * b)) & 255);
  };
  for (unsigned i = 0; i < count; ++i) {
    op(abc::Opcode::kLDA_V8);
    byte(3);
    op(abc::Opcode::kSTA_V8);
    byte(0);
    if (copies) {
      op(abc::Opcode::kMOV_V8_V8);
      byte(1);
      byte(0);
      op(abc::Opcode::kMOV_V8_V8);
      byte(0);
      byte(1);
    }
    std::vector<std::size_t> matches;
    for (unsigned c = 0; c < width; ++c) {
      number(c);
      op(abc::Opcode::kSTRICTNOTEQ_IMM8_V8);
      byte(0);
      byte(0);
      matches.push_back(jump(abc::Opcode::kJEQZ_IMM16));
      if (split) {
        const auto edge = jump(abc::Opcode::kJMP_IMM16);
        bind(edge, code.size());
      }
    }
    const auto miss = jump(abc::Opcode::kJMP_IMM16);
    std::optional<std::size_t> skipped;
    for (unsigned c = 0; c < width; ++c) {
      bind(matches[c], code.size());
      if (skipped && c == width - 1) bind(*skipped, code.size());
      op(abc::Opcode::kDEBUGGER);
      number(c + 3);
      op(abc::Opcode::kSTA_V8);
      byte(2);
      if (nonadjacent && c == 0) skipped = jump(abc::Opcode::kJMP_IMM16);
    }
    bind(miss, code.size());
  }
  op(abc::Opcode::kLDUNDEFINED);
  op(abc::Opcode::kRETURN);
  const auto body = test::Body(code);
  const auto frame =
      Take<abc::FrameLayout>(abc::BuildFrameLayout(2 + 1, 1, {}));
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = Take<FunctionInput>(FunctionInput::Build(
      identity, {100}, {200}, body, frame, InputCaptureMode::kClosed));
  return Take<FunctionValues>(BuildFunctionValues(input, body, frame)).graph;
}
void CheckGraph(unsigned count, unsigned width = 2) {
  const auto graph = Graph(count, false, false, width);
  const auto result = BuildControlPlan(graph);
  if (!std::holds_alternative<ControlPlan>(result))
    std::cerr << "control " << unsigned(ControlResultError(result)) << " count "
              << count << '\n';
  const auto plan = Take<ControlPlan>(result);
  Check(plan.switches && plan.switches->regions.size() == count);
  Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(graph, plan)));
  auto bad = plan;
  auto proof = std::make_shared<SwitchRegions>(*plan.switches);
  proof->ports.front().edge.value++;
  bad.switches = proof;
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, bad)));
  bad = plan;
  proof = std::make_shared<SwitchRegions>(*plan.switches);
  proof->clauses.front().test->discriminant.value++;
  bad.switches = proof;
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, bad)));
  bad = plan;
  bad.edges.front().owner.value++;
  Check(std::holds_alternative<ControlError>(VerifyControlPlan(graph, bad)));
  const auto passes = [&](std::size_t bytes, std::uint64_t work) {
    return std::holds_alternative<ControlPlan>(
        BuildControlPlan(graph, {bytes, work}));
  };
  auto exact = [&](bool memory) {
    std::uint64_t lo = 0, hi = memory ? analysis::SemanticBudget{}.bytes
                                      : analysis::SemanticBudget{}.work;
    while (lo < hi) {
      const auto m = lo + (hi - lo) / 2;
      if (passes(memory ? m : analysis::SemanticBudget{}.bytes,
                 memory ? analysis::SemanticBudget{}.work : m))
        hi = m;
      else
        lo = m + 1;
    }
    return lo;
  };
  const auto bytes = exact(true), work = exact(false);
  Check(bytes && work && passes(bytes, work));
  Check(ControlResultError(BuildControlPlan(graph, {bytes - 1, work})) ==
        ControlError::kResourceLimit);
  Check(ControlResultError(BuildControlPlan(graph, {bytes, work - 1})) ==
        ControlError::kWorkLimit);
  std::cout << count << " x " << width << " casesets: bytes=" << bytes
            << " work=" << work << '\n';
}
void Tree(unsigned count, unsigned width = 2) {
  const auto graph = Graph(count, false, false, width);
  const auto control = Take<ControlPlan>(BuildControlPlan(graph));
  const auto plan = Take<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity.profile = abc::ProfileId::kDynamic1206;
  syntax.method = {100};
  syntax.strictness = Strictness::kStrict;
  syntax.callable = CallableKind::kFunction;
  syntax.names = {u"kind"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> entries(graph.ops.size());
  ExpressionConstants constants{entries, 1, 0};
  const auto built = Take<ConstructedFunction>(
      BuildFunctionTree(graph, control, plan, syntax, constants));
  Take<TreeSemantics>(
      VerifyTreeSemantics(graph, control, plan, built.tree, constants));
  const auto flow =
      Take<TreeFlow>(VerifyTreeFlow(graph, control, plan, built.tree));
  const auto passes = [&](std::size_t bytes, std::uint64_t work) {
    return std::holds_alternative<TreeFlow>(
        VerifyTreeFlow(graph, control, plan, built.tree, {bytes, work}));
  };
  auto exact = [&](bool memory) {
    std::uint64_t lo = 0, hi = memory ? analysis::SemanticBudget{}.bytes
                                      : analysis::SemanticBudget{}.work;
    while (lo < hi) {
      const auto m = lo + (hi - lo) / 2;
      if (passes(memory ? m : analysis::SemanticBudget{}.bytes,
                 memory ? analysis::SemanticBudget{}.work : m))
        hi = m;
      else
        lo = m + 1;
    }
    return lo;
  };
  const auto bytes = exact(true), work = exact(false);
  Check(bytes && work && passes(bytes, work));
  Check(std::get<TreeFlowError>(VerifyTreeFlow(graph, control, plan, built.tree,
                                               {bytes - 1, work})) ==
        TreeFlowError::kResourceLimit);
  Check(std::get<TreeFlowError>(VerifyTreeFlow(graph, control, plan, built.tree,
                                               {bytes, work - 1})) ==
        TreeFlowError::kWorkLimit);
  std::optional<StmtId> first;
  for (std::uint32_t i = 0; i < built.tree.statements.size(); ++i)
    if (std::holds_alternative<SwitchStmt>(
            built.tree.statements[i].statement)) {
      first = StmtId{i};
      break;
    }
  Check(first.has_value());
  const auto& sw =
      std::get<SwitchStmt>(built.tree.statements[first->value].statement);
  const auto& clauses =
      std::get<BlockStmt>(built.tree.statements[sw.body.value].statement);
  auto bad = built.tree;
  std::swap(bad.children[clauses.first_child],
            bad.children[clauses.first_child + 1]);
  Check(std::holds_alternative<TreeFlowError>(
      VerifyTreeFlow(graph, control, plan, bad)));
  bad = built.tree;
  const auto case_id = bad.children[clauses.first_child];
  auto& clause =
      std::get<SwitchCaseStmt>(bad.statements[case_id.value].statement);
  clause.condition.reset();
  Check(std::holds_alternative<TreeFlowError>(
      VerifyTreeFlow(graph, control, plan, bad)));
  std::cout << count << " x " << width
            << " syntax-flow: admitted_bytes=" << bytes
            << " work=" << work << " retained_check_bytes=" << flow.allocated_bytes << '\n';
}

void Labels() {
  FunctionTree tree;
  tree.body = {0};
  tree.statements = {
      {BlockStmt{{0}, 0, 1}, {}}, {LoopStmt{{2}}, {}},
      {BlockStmt{{1}, 1, 1}, {}}, {SwitchStmt{{4}, {}}, {}},
      {BlockStmt{{2}, 2, 1}, {}}, {SwitchCaseStmt{{}, {6}}, {}},
      {BlockStmt{{3}, 3, 2}, {}}, {BreakStmt{{1}}, {}},
      {ContinueStmt{{1}}, {}},
  };
  tree.children = {{1}, {3}, {5}, {7}, {8}};
  Take<TreeStorage>(AssignTreeLabels(tree));
  const auto label = std::get<LoopStmt>(tree.statements[1].statement).label;
  Check(label &&
        std::get<BreakStmt>(tree.statements[7].statement).label == label);
  Check(!std::get<ContinueStmt>(tree.statements[8].statement).label);
  std::get<BreakStmt>(tree.statements[7].statement).target = {3};
  Take<TreeStorage>(AssignTreeLabels(tree));
  Check(std::get<BreakStmt>(tree.statements[7].statement).label ==
        std::get<SwitchStmt>(tree.statements[3].statement).label);
  Check(!std::get<LoopStmt>(tree.statements[1].statement).label);
}

}  // namespace
int main() {
  Labels();
  Tree(1);
  for (auto n : {1U, 200U, 201U, 400U, 800U}) {
    CheckGraph(n);
    if (n > 1) {
      Tree(n);
      CheckGraph(1, n);
      Tree(1, n);
    }
  }
  const auto nonadjacent = BuildControlPlan(Graph(1, false, false, 3, true));
  Check(!std::holds_alternative<ControlPlan>(nonadjacent));
  Check(ControlResultError(nonadjacent) == ControlError::kCrossEntry);
  for (bool c : {false, true})
    for (bool s : {false, true}) {
      const auto g = Graph(1, c, s);
      const auto p = Take<ControlPlan>(BuildControlPlan(g));
      Check(std::holds_alternative<std::uint64_t>(VerifyControlPlan(g, p)));
    }
}
