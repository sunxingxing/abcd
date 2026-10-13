#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/phase_composition.h"
#include "analysis/guarded_fixture.h"
#include "source/action_regions.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
auto Run(std::size_t count, bool copies = false, bool split = false, bool reordered = false) {
  return test::WithGuardedGraph(count, copies, split, false, [&](auto& graph, const auto& body, const auto& input) {
    const auto raw = std::get<GuardedReads>(ProveGuardedReads(graph));
    test::Check(raw.reads.size() == count);
    const auto build = [&](SemanticBudget budget = {}) {
      return BuildPhasedEvaluationPlan(graph, budget, IncomingCells::kConditional, {}, {}, {}, {}, ClosureWrites::kLanguage, {}, raw.reads);
    };
    const auto built = build(); test::Check(std::holds_alternative<EvaluationPlan>(built));
    const auto& plan = std::get<EvaluationPlan>(built);
    test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, plan)));
    const auto& pair = raw.reads.front();
    test::Check(plan.operations[pair.read.value].kind == EvaluationKind::kRoot &&
        plan.operations[pair.guard.value].kind == EvaluationKind::kGuardedCellRead);
    auto changed = plan; changed.operations[pair.guard.value].kind = EvaluationKind::kCellGuard;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
    changed = plan; changed.guarded_reads.front().value.value++;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
    changed = plan; changed.guarded_reads.front().success = graph.ops[pair.read.value].block;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
    changed = plan; changed.guarded_peers[pair.guard.value].reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
    changed = plan; changed.operations[pair.read.value] = {EvaluationKind::kInline, graph.ops[pair.guard.value].first_use, {}};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
    auto wrong = graph;
    for (auto& exit : wrong.exceptional_exits) if (exit.op == pair.guard) exit.handler.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(wrong, plan)));
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
    const ExceptionTreeInput exceptions{body, input, regions, control};
    FunctionSyntax syntax{}; syntax.owner = {2}; syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict; syntax.names = {u"capture"};
    test::Check(plan.cells && plan.cells->cells.cells.size() == 1);
    syntax.captures.push_back({CellId{0}, {{1}, {0}}, {0}});
    std::vector<ExpressionConstant> constants(graph.ops.size());
    const auto result = BuildExceptionFunctionDraft(graph, exceptions, plan, syntax, {constants, 1});
    test::Check(std::holds_alternative<ExceptionFunctionDraft>(result));
    const auto& draft = std::get<ExceptionFunctionDraft>(result);
    test::Check(draft.flow && draft.flow->original && draft.flow->original->action_regions.size() == count);
    test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, plan, draft.tree, {constants, 1})));
    test::CheckActionRegions(draft.tree, *draft.flow->original);
    if (count == 1) {
      auto tree = draft.tree;
      const auto expr = *draft.flow->expressions[pair.read.value];
      tree.expressions[expr.value].expression = TreeLiteral{UndefinedLiteral{}};
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph, exceptions, plan, tree, {constants, 1})));
      // No independent root can silently turn into a composed operand.
      changed = plan; changed.phase_compositions.push_back(pair.read);
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
    }
    // Actual returned work admits the pass; one fewer unit refuses. Byte
    // boundary is found by a slow test-only oracle, never production retries.
    test::Check(std::holds_alternative<EvaluationPlan>(build({SemanticBudget{}.bytes, plan.work})));
    test::Check(std::get<EvaluationFailure>(build({SemanticBudget{}.bytes, plan.work - 1})) == EvaluationError::kWorkLimit);
    std::size_t low = 0, high = SemanticBudget{}.bytes;
    while (low + 1 < high) {
      const auto mid = low + (high - low) / 2;
      if (std::holds_alternative<EvaluationPlan>(build({mid, SemanticBudget{}.work}))) high = mid;
      else low = mid;
    }
    test::Check(std::holds_alternative<EvaluationPlan>(build({high, SemanticBudget{}.work})));
    test::Check(std::get<EvaluationFailure>(build({high - 1, SemanticBudget{}.work})) == EvaluationError::kResourceLimit);
    return std::pair{plan.work, high};
  }, reordered);
}
}
int main() {
  std::pair<std::uint64_t, std::size_t> previous{};
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    const auto cost = Run(count);
    if (count > 200) test::Check(cost.first <= 3 * previous.first && cost.second <= 3 * previous.second);
    previous = cost;
  }
  Run(1, true); Run(1, true, true); Run(1, false, false, true);
}
