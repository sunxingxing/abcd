#include "analysis/guarded_fixture.h"
#include "source/action_regions.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/phase_composition.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/guarded_call_json.h"

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
auto Run(std::size_t count, bool copies = false, bool split = false,
         test::GuardedCallCase variant = test::GuardedCallCase::kPlain) {
  return test::WithGuardedGraph(count, copies, split, false, [&](auto& graph, const auto& body, const auto& input) {
    const auto reads = std::get<GuardedReads>(ProveGuardedReads(graph));
    test::Check(reads.reads.size() == count);
    const auto build = [&](SemanticBudget budget = {}) {
      return BuildPhasedEvaluationPlan(graph, budget, IncomingCells::kConditional,
          {}, {}, {}, {}, ClosureWrites::kLanguage, {}, reads.reads);
    };
    const auto built = build();
    if (const auto* e = std::get_if<EvaluationFailure>(&built)) std::cerr << "build " << count << " copies=" << copies << " split=" << split << " error=" << unsigned(e->kind) << "\n";
    test::Check(std::holds_alternative<EvaluationPlan>(built));
    const auto& plan = std::get<EvaluationPlan>(built);
    for (const auto& pair : reads.reads) {
      if (plan.operations[pair.read.value].kind != EvaluationKind::kInline) std::cerr << "not inline count="<<count<<" copy="<<copies<<" split="<<split<<" read="<<pair.read.value<<"\n";
      test::Check(plan.operations[pair.read.value].kind == EvaluationKind::kInline);
      test::Check(plan.operations[pair.guard.value].kind == EvaluationKind::kGuardedCellRead);
    }
    test::Check(plan.checked_compositions.size() == count);
    test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, plan)));
    const auto rejected = [&](const auto& changed) { test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed))); };
    const auto& pair = reads.reads.front();
    auto changed = plan; changed.checked_compositions.clear(); rejected(changed);
    changed = plan; changed.checked_compositions.push_back(changed.checked_compositions.front()); rejected(changed);
    changed = plan; changed.guarded_reads.clear(); rejected(changed);
    changed = plan; changed.guarded_reads.push_back(pair); rejected(changed);
    changed = plan; changed.guarded_reads.front().success = graph.ops[pair.read.value].block; rejected(changed);
    changed = plan; changed.operations[pair.guard.value].kind = EvaluationKind::kCellGuard; rejected(changed);
    changed = plan; changed.operations[pair.read.value].consumer = graph.ops[pair.guard.value].first_use; rejected(changed);
    auto wrong = graph;
    for (auto& exit : wrong.exceptional_exits) if (exit.op == pair.guard) exit.handler.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(wrong, plan)));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      if (!std::holds_alternative<ValueInvoke>(graph.ops[i].operation)) continue;
      auto wrong_receiver = graph;
      wrong_receiver.uses[graph.ops[i].first_use.value + 1].operand = pair.value;
      test::Check(std::holds_alternative<CallPlanFailure>(BuildPhasedCallPlan(wrong_receiver, plan)));
      break;
    }
    if (count == 1) {
      const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
      const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
      const ExceptionTreeInput exceptions{body, input, regions, control};
      FunctionSyntax syntax{}; syntax.owner = {2}; syntax.callable = CallableKind::kFunction;
      syntax.strictness = Strictness::kStrict; syntax.names = {u"capture", u"method"};
      syntax.captures.push_back({CellId{0}, {{1}, {0}}, {0}});
      std::vector<ExpressionConstant> constants(graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i)
        if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{1};
      const auto built_tree = BuildExceptionFunctionDraft(graph, exceptions, plan, syntax, {constants, 2});
      test::Check(std::holds_alternative<ExceptionFunctionDraft>(built_tree));
      const auto& tree = std::get<ExceptionFunctionDraft>(built_tree);
      test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, plan, tree.tree, {constants, 2})));
      test::CheckActionRegions(tree.tree, *tree.flow->original);
      auto changed_tree = tree.tree;
      const auto expr = *tree.flow->expressions[pair.read.value];
      changed_tree.expressions[expr.value].expression = TreeLiteral{UndefinedLiteral{}};
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph, exceptions, plan, changed_tree, {constants, 2})));
    }
    test::Check(std::holds_alternative<EvaluationPlan>(build({SemanticBudget{}.bytes, plan.work})));
    test::Check(std::get<EvaluationFailure>(build({SemanticBudget{}.bytes, plan.work - 1})) == EvaluationError::kWorkLimit);
    std::size_t low = 0, high = SemanticBudget{}.bytes;
    while (low + 1 < high) {
      const auto mid = low + (high - low) / 2;
      if (std::holds_alternative<EvaluationPlan>(build({mid, SemanticBudget{}.work}))) high = mid; else low = mid;
    }
    test::Check(std::get<EvaluationFailure>(build({high - 1, SemanticBudget{}.work})) == EvaluationError::kResourceLimit);
    return std::pair{plan.work, high};
  }, false, true, variant);
}
void Negative(test::GuardedCallCase variant, bool pure_wrapper = false) {
  test::WithGuardedGraph(1, false, false, false, [&](auto& graph, const auto&, const auto&) {
    const auto reads = std::get<GuardedReads>(ProveGuardedReads(graph));
    const auto built = BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kConditional,
        {}, {}, {}, {}, ClosureWrites::kLanguage, {}, reads.reads);
    test::Check(std::holds_alternative<EvaluationPlan>(built));
    const auto& plan = std::get<EvaluationPlan>(built);
    if (variant == test::GuardedCallCase::kSatisfied) {
      test::Check(plan.checked_compositions.empty());
      test::Check(plan.operations[reads.reads.front().read.value].kind == EvaluationKind::kRoot);
      test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, plan)));
      auto unchanged = plan; TreeCheckBudget budget(SemanticBudget{});
      ComposeCheckedCalls(graph, unchanged, budget);
      test::Check(unchanged.checked_compositions.empty() && unchanged.checked_failures.empty());
      for (std::size_t i = 0; i < plan.operations.size(); ++i) {
        test::Check(unchanged.operations[i].kind == plan.operations[i].kind &&
            unchanged.operations[i].consumer == plan.operations[i].consumer &&
            unchanged.operations[i].temporary == plan.operations[i].temporary);
      }
      return;
    }
    if (variant == test::GuardedCallCase::kTwoReads || variant == test::GuardedCallCase::kPureWrapper) {
      test::Check(reads.reads.size() == (variant == test::GuardedCallCase::kTwoReads ? 2U : 1U));
      test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, plan)));
      return;
    }
    test::Check(plan.checked_compositions.empty());
    const auto failure = BuildPhasedCallPlan(graph, plan);
    test::Check(std::holds_alternative<CallPlanFailure>(failure));
    if (variant == test::GuardedCallCase::kCatchState || variant == test::GuardedCallCase::kOutsideCatch) {
      const auto& detail = std::get<CallPlanFailure>(failure);
      test::Check(detail.reference && detail.reference->checked && detail.reference->checked->kind == GuardedCallError::kCatchState);
    }
    auto changed = plan;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const OpId call{static_cast<std::uint32_t>(i)};
      if (const auto property = MemberProperty(graph, plan, call)) {
        changed.checked_compositions.push_back(call);
        changed.operations[property->value] = {EvaluationKind::kInline, graph.ops[i].first_use, {}};
        changed.operations[reads.reads.front().read.value] = {EvaluationKind::kInline,
            UseId{graph.ops[i].first_use.value + 2}, {}};
      }
    }
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)));
  }, false, true, variant, pure_wrapper);
}

}
int main() {
  std::pair<std::uint64_t, std::size_t> previous{};
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    const auto cost = Run(count);
    if (count > 200) test::Check(cost.first <= 3 * previous.first && cost.second <= 3 * previous.second);
    previous = cost;
  }
  Run(1, true); Run(1, true, true);
  previous = {};
  for (const auto count : {200U, 201U, 400U, 800U}) {
    const auto cost = Run(count, false, false, test::GuardedCallCase::kPureWrapper);
    if (count > 200) test::Check(cost.first <= 3 * previous.first && cost.second <= 3 * previous.second);
    previous = cost;
    std::cout << "guarded-pure " << count << " work=" << cost.first << " bytes=" << cost.second << '\n';
  }
  Run(1, true, true, test::GuardedCallCase::kPureWrapper);
  for (const auto variant : {test::GuardedCallCase::kForeign, test::GuardedCallCase::kCatchState,
      test::GuardedCallCase::kOutsideCatch, test::GuardedCallCase::kPureWrapper,
      test::GuardedCallCase::kTwoReads, test::GuardedCallCase::kExtraUse, test::GuardedCallCase::kSatisfied}) Negative(variant);
  for (const auto variant : {test::GuardedCallCase::kForeign,
      test::GuardedCallCase::kExtraUse}) Negative(variant, true);
  GuardedCallFailure failure{GuardedCallError::kCatchState, {0}, {1}, {2}, {3}};
  failure.read_pc = abc::CodeOffset{0}; failure.guard_pc = abc::CodeOffset{9}; failure.guard_working_pc = abc::CodeOffset{6};
  test::Check(ParseGuardedCall(nlohmann::json::parse(GuardedCallJson(failure).dump())) == failure);
  auto wrong = GuardedCallJson(failure); wrong["kind"] = 255U;
  test::Check(!ParseGuardedCall(wrong));
}
