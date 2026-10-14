#include "../../../src/source/exception_tree.h"
#include "../../../src/source/completion_plan.h"
#include "../../../src/source/cell_lifetime.h"
#include "../../../src/source/hole_protocol.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "source/protocol_values.h"
#include "analysis/ssa_fixture.h"
#include "analysis/shared_exit_fixture.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/source/async_completion.h"

#include <sstream>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct ExpectedCompletion { std::size_t regions, operations; };
enum class CompositionStage { kSource, kCompletion, kHoleImport, kHoleSource, kHoleFinally, kHolePlan };
template <class T, class... Alternatives>
T Take(std::variant<Alternatives...> value,
    std::source_location where = std::source_location::current()) {
  if (!std::holds_alternative<T>(value)) {
    std::cerr << "composition pipeline failure at line " << where.line() << '\n';
    std::visit([](const auto& error) {
      if constexpr (std::is_enum_v<std::decay_t<decltype(error)>>)
        std::cerr << "error " << static_cast<unsigned>(error) << '\n';
    }, value);
  }
  test::Check(std::holds_alternative<T>(value));
  return std::get<T>(std::move(value));
}
void Emit(const abc::MethodBody& body, abc::ProfileId profile, std::uint32_t locals,
    std::uint32_t parameters, bool staging, std::ostream* output,
    std::optional<ScopeKind> cell_scope = {}, std::size_t expected_loops = 1,
    bool expect_finalizer = false, std::optional<ExpectedCompletion> expected_completions = {},
    CompositionStage stage = CompositionStage::kSource, std::size_t finalizer_count = 1,
    std::span<const AwaitSite> awaits = {}, bool reject_decision = false, std::uint64_t* draft_work = nullptr);

void LoopCarried(abc::ProfileId profile, std::ostream* output) {
  // x = initial; try { while (next()) { x = step(x); next(); } } catch (_) {}
  // return x. Three throwing sites must observe the current loop-carried x,
  // never a failed call result or the function's original entry value.
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 15,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 3,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-17),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {4}; region.length = 19; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {26}; handler.length = 3;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 3, true, output);
}
void Emit(const abc::MethodBody& body, abc::ProfileId profile, std::uint32_t locals,
    std::uint32_t parameters, bool staging, std::ostream* output, std::optional<ScopeKind> cell_scope,
    std::size_t expected_loops, bool expect_finalizer, std::optional<ExpectedCompletion> expected_completions,
    CompositionStage stage, std::size_t finalizer_count, std::span<const AwaitSite> awaits, bool reject_decision, std::uint64_t* draft_work) {
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(locals, parameters, {}));
  const auto flow = Take<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, awaits, &frame));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = Take<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (std::holds_alternative<MethodEntry>(value.origin) && reg && reg->value >= frame.locals)
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  }
  const auto environments = Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto lexical = Take<ExceptionLocals>(ProveExceptionLocals(flow, environments));
  IdfQuota value_quota{kDefaultSemanticWork};
  const auto values = Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_quota, {}, PendingExceptionValues::kLanguage, lexical.loads, {}, CallResultValues::kLanguage));
  if (stage == CompositionStage::kHoleImport) {
    const auto language = VerifyExceptionInputs(flow, ssa, environments, values);
    test::Check(std::holds_alternative<ExceptionInputFailure>(language));
    test::Check(std::get<ExceptionInputFailure>(language).kind == ExceptionInputError::kInternalValue);
  }
  const auto proof = Take<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values,
      kDefaultSemanticWork, nullptr, (stage == CompositionStage::kHoleImport || stage == CompositionStage::kHoleSource || stage == CompositionStage::kHoleFinally || stage == CompositionStage::kHolePlan) ?
          ExceptionInputDomain::kHoleTransport : ExceptionInputDomain::kLanguage));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto graph = Take<ValueProgram>(ImportPhasedValues(input));
  if (stage == CompositionStage::kHoleImport) {
    std::size_t holes = 0, mixed = 0;
    for (const auto& op : graph.ops) if (std::holds_alternative<ValueHole>(op.operation)) {
      ++holes; test::Check(op.result.has_value());
      test::Check(graph.values[op.result->value].domain == ValueKinds::Hole());
      test::Check(op.use_count == 0);
    }
    for (const auto& value : graph.values)
      if (value.domain.Contains(VmValueKind::kHole) && value.domain.Contains(VmValueKind::kLanguage)) ++mixed;
    test::Check(holes == 2 && mixed > 0);
    const auto protocol = Take<HoleProtocol>(AnalyzeHoleProtocol(graph));
    test::Check(protocol.comparisons.size() == 1 && protocol.rethrows.size() == 1);
    const auto compared = protocol.comparisons.front().operation;
    const auto rethrown = protocol.rethrows.front().operation;
    test::Check(protocol.rethrows.front().comparison == compared);
    test::Check(graph.origins[graph.ops[compared.value].origin.value].pc.value == 39);
    test::Check(graph.origins[graph.ops[rethrown.value].origin.value].pc.value == 46);
    auto changed = graph;
    changed.ops[rethrown.value].operation = ValueReturn{};
    test::Check(std::get<HoleProtocolError>(AnalyzeHoleProtocol(changed)) == HoleProtocolError::kEscape);
    changed = graph;
    std::get<ValueBinary>(changed.ops[compared.value].operation).op = BinaryOperator::kEqual;
    test::Check(std::get<HoleProtocolError>(AnalyzeHoleProtocol(changed)) == HoleProtocolError::kUnguardedThrow);
    changed = graph;
    std::get<ValueBinary>(changed.ops[compared.value].operation).op = BinaryOperator::kAdd;
    test::Check(std::get<HoleProtocolError>(AnalyzeHoleProtocol(changed)) == HoleProtocolError::kEscape);
    changed = graph;
    const auto comparison_use = changed.ops[compared.value].first_use.value;
    std::swap(changed.uses[comparison_use].operand, changed.uses[comparison_use + 1].operand);
    // Operand-chain links now deliberately mismatch; malformed correspondence
    // must report input failure, not masquerade as an unsupported protocol.
    test::Check(std::get<HoleProtocolError>(AnalyzeHoleProtocol(changed)) == HoleProtocolError::kInput);
    const auto evaluation = Take<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
    test::Check(evaluation.holes.has_value());
    auto altered = evaluation;
    altered.holes.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, altered)));
    altered = evaluation;
    altered.holes->comparisons.front().equal = !altered.holes->comparisons.front().equal;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, altered)));
    return; // Raw VM transport is not a source or finalizer completion claim.
  }
  const auto regions = Take<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto built_control = BuildExceptionControl(body, graph, input, regions);
  if (const auto* error = std::get_if<ExceptionControlError>(&built_control))
    std::cerr << "loop-carried control error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ExceptionControl>(built_control));
  const auto& control = std::get<ExceptionControl>(built_control);
  test::Check(control.skeleton.loops.size() == expected_loops);
  if (expected_loops > 1) {
    auto changed = control;
    changed.skeleton.loops.front().parent.reset();
    test::Check(std::holds_alternative<ExceptionControlError>(
        VerifyExceptionControl(body, graph, input, regions, changed)));
    changed = control;
    changed.skeleton.loops.clear();
    test::Check(std::holds_alternative<ExceptionControlError>(
        VerifyExceptionControl(body, graph, input, regions, changed)));
  }
  const auto planned = BuildPhasedEvaluationPlan(graph);
  if (const auto* error = std::get_if<EvaluationFailure>(&planned))
    std::cerr << "composition evaluation error " << static_cast<unsigned>(error->kind) << '\n';
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  const auto completions = Take<CompletionPlan>(BuildCompletionPlan(graph, regions, control, evaluation));
  if (stage == CompositionStage::kHolePlan) {
    test::Check(completions.finalizers.size() == finalizer_count);
    for (const auto& finalizer : completions.finalizers) test::Check(finalizer.shared_guard.has_value());
    return; // Deep topology coverage, not complete-source acceptance.
  }
  if (expected_completions) {
    if (completions.finalizers.size() != expected_completions->regions)
      std::cerr << "completion candidates " << completions.finalizers.size() << '\n';
    test::Check(completions.finalizers.size() == expected_completions->regions);
    for (const auto& finalizer : completions.finalizers) {
      test::Check(std::holds_alternative<ReturnCompletion>(finalizer.normal));
      test::Check(std::holds_alternative<ThrowCompletion>(finalizer.exceptional));
      test::Check(finalizer.cleanup.size() == expected_completions->operations);
      const auto pending = std::get<ValueId>(std::get<ReturnCompletion>(finalizer.normal).value);
      const auto producer = std::get<OpResult>(graph.values[pending.value].origin).op;
      test::Check(graph.origins[graph.ops[producer.value].origin.value].pc.value == 2);
      test::Check(std::get<ValueId>(std::get<ThrowCompletion>(finalizer.exceptional).value) ==
          regions.regions[finalizer.region.value].binding->value);
      test::Check(std::holds_alternative<ValueInvoke>(graph.ops[finalizer.cleanup.front().normal.value].operation));
      test::Check(std::holds_alternative<ValueInvoke>(graph.ops[finalizer.cleanup.front().exceptional.value].operation));
    }
  }
  if (stage == CompositionStage::kCompletion) {
    test::Check(expected_completions.has_value() && !output);
    return; // Algorithm boundary test, explicitly not source acceptance.
  }
  if (staging) test::Check(evaluation.exceptions && !evaluation.exceptions->slots.empty());
  else if (!evaluation.cells && expected_loops == 1 && !expect_finalizer && !expected_completions) {
    test::Check(evaluation.phis.has_value());
    bool empty_schedule = false;
    for (const auto& bundle : evaluation.phis->bundles)
      empty_schedule |= bundle.schedule.steps.empty();
    test::Check(empty_schedule); // Regression: no orphan origin for erased self-copies.
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  FunctionSyntax syntax{};
  syntax.callable = awaits.empty() ? CallableKind::kFunction : CallableKind::kAsyncFunction;
  syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"};
  syntax.parameters.resize(frame.parameters);
  if (expect_finalizer && stage != CompositionStage::kHoleFinally) {
    // Reserve an existing spelling. Source parameter names currently exclude
    // the '$' namespace, but recovery must not assume every input name does.
    syntax.names = {u"$completion0"};
  }
  const ExpressionConstants resolved{constants, static_cast<std::uint32_t>(syntax.names.size()), 1, TextId{0}};
  const ExceptionTreeInput context{body, input, regions, control};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  if (reject_decision) {
    // Round 60 handles selected-arm phi copies. The independent later
    // boundary still refuses a conditional statement or catch staging; neither
    // can be hoisted out of the short-circuit path.
    const auto* failure = std::get_if<FunctionTreeFailure>(&built);
    test::Check(failure && failure->kind == FunctionTreeError::kOwnership && failure->tree);
    test::Check(failure->tree->obligation == (staging ? TreeObligation::kCatchStaging :
        TreeObligation::kExpressionOwnership));
    return; // Neither refusal permits hoisting statements or catch state.
  }
  if (const auto* error = std::get_if<FunctionTreeFailure>(&built))
    std::cerr << "loop-carried tree error " << static_cast<unsigned>(error->kind) << '\n';
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  if (draft_work) *draft_work = std::get<ExceptionFunctionDraft>(built).work;
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  if (!awaits.empty()) {
    auto changed = tree;
    changed.callable = CallableKind::kFunction;
    const auto invalid_context = VerifyTreeStructure(changed);
    test::Check(std::get_if<TreeStructureError>(&invalid_context) &&
        std::get<TreeStructureError>(invalid_context) == TreeStructureError::kUnsupportedSyntax);
    bool found_await = false;
    for (auto& expression : changed.expressions) {
      if (std::holds_alternative<AwaitExpr>(expression.expression)) {
        found_await = true;
        expression.expression = TreeLiteral{UndefinedLiteral{}};
      }
    }
    test::Check(found_await);
    changed.callable = CallableKind::kAsyncFunction;
    test::Check(std::holds_alternative<TreeSemanticError>(
        VerifyExceptionTreeSemantics(graph, context, evaluation, changed, resolved)));
  }
  if (stage == CompositionStage::kHoleFinally) {
    // A construction-local reuse must not turn independent entry points into
    // trusting consumers of a changed control projection.
    auto changed_control = control;
    changed_control.inactive_phases.push_back(WorkBlockId{0});
    const ExceptionTreeInput changed_context{body, input, regions, changed_control};
    const auto changed_flow = VerifyExceptionTreeFlow(graph, changed_context, evaluation, tree);
    test::Check(std::get_if<TreeFlowError>(&changed_flow) &&
        std::get<TreeFlowError>(changed_flow) == TreeFlowError::kControl);
    const auto changed_draft = BuildExceptionFunctionDraft(graph, changed_context, evaluation, syntax, resolved);
    test::Check(std::get_if<FunctionTreeFailure>(&changed_draft) &&
        std::get<FunctionTreeFailure>(changed_draft) == FunctionTreeError::kControl);
    std::size_t finalizers = 0;
    for (const auto& statement : tree.statements)
      if (const auto* region = std::get_if<TryStmt>(&statement.statement); region && region->finalizer)
        ++finalizers;
    test::Check(finalizers == finalizer_count && tree.finalizers.size() == finalizer_count);
    auto changed = tree;
    changed.finalizers.clear();
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
    changed = tree;
    changed.finalizers.push_back(changed.finalizers.front());
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
  }
  if (stage == CompositionStage::kHoleSource) {
    test::Check(tree.hole.has_value());
    auto changed = tree;
    changed.hole.reset();
    test::Check(std::holds_alternative<TreeFlowError>(
        VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
    changed = tree;
    for (const auto& statement : tree.statements) {
      const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
      if (!declaration || declaration->symbol != *tree.hole) continue;
      test::Check(declaration->initializer.has_value());
      changed.expressions[declaration->initializer->value].expression = TreeLiteral{BooleanLiteral{false}};
    }
    test::Check(std::holds_alternative<TreeFlowError>(
        VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
  }
  if (expect_finalizer && stage != CompositionStage::kHoleFinally) {
    std::size_t finalizers = 0;
    for (const auto& statement : tree.statements)
      if (const auto* region = std::get_if<TryStmt>(&statement.statement); region && region->finalizer)
        ++finalizers;
    if (finalizers != finalizer_count) std::cerr << "missing recovered finalizer: " << finalizers << '\n';
    test::Check(finalizers == finalizer_count);
    test::Check(tree.finalizers.size() == finalizer_count);
    const auto enclosure = tree.finalizers.front().enclosure;
    const auto block = std::get<BlockStmt>(tree.statements[enclosure.value].statement);
    const auto declaration = std::get<DeclarationStmt>(
        tree.statements[tree.children[block.first_child].value].statement);
    test::Check(tree.names[tree.symbols[declaration.symbol.value].name.value] != syntax.names.front());
    const auto reject = [&](const FunctionTree& changed) {
      test::Check(std::holds_alternative<TreeFlowError>(
          VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
    };
    auto changed = tree;
    changed.expressions[declaration.initializer->value].expression = TreeLiteral{BooleanLiteral{true}};
    reject(changed); // The entry path must not inherit an exceptional completion.
    changed = tree;
    changed.finalizers.clear();
    reject(changed); // An unexplained finalizer cannot bypass original CFG edges.
    changed = tree;
    changed.finalizers.push_back(changed.finalizers.front());
    reject(changed);
    changed = tree;
    changed.finalizers.front().enclosure = StmtId{static_cast<std::uint32_t>(tree.statements.size())};
    reject(changed);
    const auto outer = std::get<TryStmt>(tree.statements[tree.children[block.first_child + 1].value].statement);
    const auto protected_body = std::get<BlockStmt>(tree.statements[outer.body.value].statement);
    if (protected_body.child_count > 2) {
      changed = tree;
      const auto copy_id = tree.children[protected_body.first_child + 1];
      const auto copy = std::get<ExpressionStmt>(tree.statements[copy_id.value].statement);
      const auto rhs = std::get<AssignmentExpr>(tree.expressions[copy.expression.value].expression).value;
      const ExprId callee{static_cast<std::uint32_t>(changed.expressions.size())};
      changed.expressions.push_back(changed.expressions[rhs.value]);
      changed.expressions[rhs.value].expression = CallExpr{callee, {0, 0}, CallReference::kValue};
      test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      reject(changed); // A potentially throwing expression is not a movable SSA snapshot.
    }
    const auto finalizer = std::get<BlockStmt>(tree.statements[outer.finalizer->value].statement);
    const auto route_id = tree.children[finalizer.first_child];
    changed = tree;
    auto& route = std::get<IfStmt>(changed.statements[route_id.value].statement);
    std::swap(route.consequent, *route.alternate);
    reject(changed); // Both copies look alike but retain different operation IDs.
    changed = tree;
    for (auto& node : changed.expressions)
      if (auto* assignment = std::get_if<AssignmentExpr>(&node.expression))
        if (const auto* target = std::get_if<SymbolTarget>(&assignment->target);
            target && target->symbol == declaration.symbol)
          changed.expressions[assignment->value.value].expression = TreeLiteral{BooleanLiteral{false}};
    reject(changed); // Catch must route the exceptional cleanup occurrence.
  }
  if (expected_completions && expected_completions->regions == 0)
    test::Check(tree.finalizers.empty());
  if (evaluation.cells) {
    std::size_t locals = 0;
    for (std::size_t i = 0; i < tree.symbols.size(); ++i) {
      const auto& symbol = tree.symbols[i];
      if (symbol.kind != BindingKind::kLocal) continue;
      ++locals;
      test::Check(symbol.scope != tree.body_scope);
      if (cell_scope) test::Check(tree.scopes[symbol.scope.value].kind == *cell_scope);
      auto hoisted = tree;
      hoisted.symbols[i].scope = tree.body_scope;
      test::Check(std::holds_alternative<TreeFlowError>(
          VerifyExceptionTreeFlow(graph, context, evaluation, hoisted)));
    }
    test::Check(locals == 1);
  }
  if (std::any_of(control.skeleton.nodes.begin(), control.skeleton.nodes.end(),
      [](const ControlNode& node) { return node.decision.has_value(); })) {
    const auto environments = Take<ValueEnvironments>(BuildPhasedValueEnvironments(graph));
    const auto cells = Take<CellPlan>(BuildPhasedCellPlan(graph, environments));
    const auto lifetimes = Take<CellLifetimes>(BuildPhasedCellLifetimes(graph, environments, cells, context));
    for (const auto& root : control.skeleton.nodes) {
      auto decision = root.decision;
      while (decision) {
        auto at = decision->evaluate;
        const auto last = decision->predicate.value_or(at);
        const auto scope = lifetimes.blocks[root.block.value];
        while (true) {
          const auto& condition = control.skeleton.nodes[at.value];
          test::Check(lifetimes.blocks[condition.block.value].value == scope.value);
          if (at == last) break;
          at = *condition.next;
        }
        decision = control.skeleton.nodes[last.value].decision;
      }
    }

    // The emitted syntax may be legal while its short-circuit ownership is
    // wrong. Check those mutations through independent flow/operand consumers.
    bool logical = false;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* condition = std::get_if<LogicalExpr>(&tree.expressions[i].expression);
      if (!condition || condition->operation != TreeLogical::kOr) continue;
      if (std::none_of(tree.statements.begin(), tree.statements.end(), [&](const auto& statement) {
            const auto* branch = std::get_if<IfStmt>(&statement.statement);
            return branch && branch->condition.value == i;
          })) continue;
      auto changed = tree;
      std::get<LogicalExpr>(changed.expressions[i].expression).operation = TreeLogical::kAnd;
      test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
      logical = true; break;
    }
    test::Check(logical);
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* sequence = std::get_if<SequenceExpr>(&tree.expressions[i].expression);
      if (!sequence) continue;
      auto changed = tree;
      auto& reordered = std::get<SequenceExpr>(changed.expressions[i].expression);
      std::swap(reordered.left, reordered.right);
      test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
      const auto* saved = std::get_if<AssignmentExpr>(&tree.expressions[sequence->left.value].expression);
      if (saved) {
        changed = tree;
        auto& target = std::get<SymbolTarget>(std::get<AssignmentExpr>(changed.expressions[sequence->left.value].expression).target);
        target.symbol = tree.parameters.front().symbol;
        test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
        changed = tree;
        changed.expressions[sequence->left.value].origin = tree.expressions[sequence->right.value].origin;
        test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
      }
      break;
    }
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* binary = std::get_if<BinaryExpr>(&tree.expressions[i].expression);
      if (!binary || (binary->operation != BinaryOperator::kStrictEqual &&
          binary->operation != BinaryOperator::kStrictNotEqual)) continue;
      auto changed = tree;
      std::get<BinaryExpr>(changed.expressions[i].expression).operation =
          binary->operation == BinaryOperator::kStrictEqual ? BinaryOperator::kStrictNotEqual : BinaryOperator::kStrictEqual;
      test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(changed)));
      test::Check(std::holds_alternative<TreeSemanticError>(
          VerifyExceptionTreeSemantics(graph, context, evaluation, changed, resolved)));
      break;
    }
  }
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, tree)));
  auto semantics = Take<TreeSemantics>(VerifyExceptionTreeSemantics(
      graph, context, evaluation, tree, resolved));
  if (stage == CompositionStage::kHoleSource) {
    std::vector<OpId> holes;
    for (unsigned i=0;i<graph.ops.size();++i)
      if (std::holds_alternative<ValueHole>(graph.ops[i].operation)) holes.push_back({i});
    test::Check(!holes.empty() && semantics.flow.original);
    test::CheckProtocolValues(tree, *semantics.flow.original, holes);
  }
  const auto family = Take<ConstructedFamily>(BuildFunctionFamily({{tree, std::move(semantics.flow), {}}}, tree.owner));
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, source)));
  if (output) *output << source.str() << ",\n";
}
void RetryCarried(abc::ProfileId profile, std::ostream* output) {
  // x = initial; while(next()) { try { x = step(x); }
  // catch(e) { observe(x, e); } } return x. The observer is outside protection.
  const std::vector<std::uint8_t> bytes = {
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 3,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), 21,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 4,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-13),
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 5,
      static_cast<std::uint8_t>(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, 0, 1,
      static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-23),
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {10}; region.length = 7; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {19}; handler.length = 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 2, 4, false, output);
}
void IterationCell(abc::ProfileId profile, std::ostream* output) {
  // Every iteration allocates a fresh lexical cell. Normal and exceptional
  // writes join before the observer reads it; pop precedes the loop backedge.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 28,
      op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), 2,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kJMP_IMM8), 4,
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3,
      op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kPOPLEXENV),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-30),
      op(abc::Opcode::kRETURNUNDEFINED)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {12}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {18}; handler.length = 2;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 3, false, output, ScopeKind::kBlock);
}
void CatchCell(abc::ProfileId profile, std::ostream* output) {
  // A catch-owned environment is allocated only on failure, initialized with
  // this iteration's pending exception, observed, and popped before re-entry.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 28,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-10),
      op(abc::Opcode::kSTA_V8), 0, op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kPOPLEXENV),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-30),
      op(abc::Opcode::kRETURNUNDEFINED)};
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {6}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {12}; handler.length = 20;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 3, false, output, ScopeKind::kCatch);
}
void CatchOuterTransfer(abc::ProfileId profile, bool stop, std::ostream* output) {
  // outer: while(nextOuter()) { while(nextInner()) {
  //   try { run(); } catch(e) { continue/break outer; } body(); } after(); }
  // Normal try exit is PC16; catch exits to the outer header (0) or return (30).
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 26,
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 14,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-14),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? 8 : -22),
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-28),
      op(abc::Opcode::kRETURNUNDEFINED)});
  abc::TryRegion region; region.offset = {12}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {22}; handler.length = 2;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 0, 5, false, output, {}, 2);
}
void CatchOuterCarried(abc::ProfileId profile, bool stop, std::ostream* output) {
  // x=initial; outer/inner loops as above, but run/body/after update x.
  // A throwing run preserves the value from the preceding successful call,
  // including a value produced by an earlier inner iteration.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 35,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 20,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-20),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? 11 : -28),
      op(abc::Opcode::kLDA_V8), 6, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-37),
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  abc::TryRegion region; region.offset = {16}; region.length = 7; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {32}; handler.length = 2;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 6, false, output, {}, 2);
}
void CatchOuterPorts(abc::ProfileId profile, std::ostream* output) {
  // One try has three destinations: normal continuation, continue outer,
  // break outer. Summary fanout must never appear as an invented source if.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 32,
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 20,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-14),
      op(abc::Opcode::kLDA_V8), 5,
      op(abc::Opcode::kJEQZ_IMM8), 4,
      op(abc::Opcode::kJMP_IMM8), 10,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-28),
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-34),
      op(abc::Opcode::kRETURNUNDEFINED)});
  abc::TryRegion region; region.offset = {12}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {22}; handler.length = 8;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 0, 6, false, output, {}, 2);
}
void NestedOuterTransfer(abc::ProfileId profile, bool stop, std::ostream* output) {
  // Inner catch transfers across its enclosing try; a body failure instead
  // reaches the outer catch, which observes it before the same outer transfer.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 32,
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 20,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-14),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? 14 : -22),
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? 8 : -28),
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-34),
      op(abc::Opcode::kRETURNUNDEFINED)});
  abc::TryRegion inner; inner.offset = {12}; inner.length = 4; inner.handler_count = 1;
  abc::TryRegion outer = inner; outer.length = 12; outer.first_handler = 1;
  abc::CatchHandler inner_handler; inner_handler.offset = {22}; inner_handler.length = 2;
  abc::CatchHandler outer_handler; outer_handler.offset = {24}; outer_handler.length = 6;
  body.tries = {inner, outer}; body.handlers = {inner_handler, outer_handler};
  Emit(body, profile, 0, 6, false, output, {}, 2);
}
void HandlerOuterTransfer(abc::ProfileId profile, bool stop, std::ostream* output) {
  // outer: while(nextOuter()) { try { run(); } catch(e) {
  // while(nextInner()) { if(skip()) continue/break outer; body(); } } after(); }
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 32,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), 20,
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 14,
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJNEZ_IMM8), static_cast<std::uint8_t>(stop ? 14 : -22),
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-16),
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-34),
      op(abc::Opcode::kRETURNUNDEFINED)});
  abc::TryRegion region; region.offset = {6}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {12}; handler.length = 18;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 0, 6, false, output, {}, 2);
}
enum class CleanupVariant { kSame, kDifferentCallee, kLateReturn, kExtraCall, kWrongRethrow };
void FinallyReturn(abc::ProfileId profile, bool recover, std::ostream* output,
    CleanupVariant variant = CleanupVariant::kSame) {
  // Lowered normal/exception cleanup copies, not source text as engine input:
  // try { return get(); } finally { cleanup(); }
  // v0 fixes the pending return/throw BEFORE cleanup; cleanup is not protected
  // by the original handler, so its failure must replace either completion.
  // This is a semantic lowered fixture, not a claim of byte-for-byte es2abc
  // output: upstream also emits hole/guard scaffolding for shared fallthrough.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)};
  switch (variant) {
    case CleanupVariant::kSame: break;
    case CleanupVariant::kDifferentCallee: bytes[16] = 3; break;
    case CleanupVariant::kLateReturn: bytes[10] = op(abc::Opcode::kSTA_V8); break;
    case CleanupVariant::kExtraCall:
      bytes.insert(bytes.begin() + 19, {op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0});
      break;
    case CleanupVariant::kWrongRethrow: bytes[20] = 1; break;
  }
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {0}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {13}; handler.length = variant == CleanupVariant::kExtraCall ? 14 : 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, variant == CleanupVariant::kDifferentCallee ? 3 : 2,
      false, output, {}, 0, recover, ExpectedCompletion{variant == CleanupVariant::kSame ? 1U : 0U, 1});
}
void FinallyChain(abc::ProfileId profile, std::ostream* output, std::uint32_t count = 2,
    CompositionStage stage = CompositionStage::kSource) {
  // The second cleanup call consumes the first cleanup call's result. Match
  // internal SSA definitions pairwise; raw ValueIds differ across the copies.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTA_V8), 0};
  const auto cleanup = [&] {
    test::Check(count != 0);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
        op(abc::Opcode::kSTA_V8), 1});
    for (std::uint32_t i = 1; i < count; ++i)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 1,
          op(abc::Opcode::kSTA_V8), 1});
  };
  cleanup();
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0});
  cleanup();
  bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {0}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = static_cast<std::uint32_t>(bytes.size()) - catch_pc;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 2, 3, false, output, {}, 0, true, ExpectedCompletion{1, count}, stage);
}
enum class ProtectedBranch { kSingle, kNested, kEarlyReturn };
enum class OverrideValue { kCleanup, kCaught, kUndefined, kArgument };
enum class NestedCompletion { kNormal, kReturn, kThrow };
void FinallyBareTransfer(abc::ProfileId profile, std::ostream* output, bool stop, bool different = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 18,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kJEQZ_IMM8), 8,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? 8 : -14),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-16),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>((stop != different) ? 2 : -20),
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kRETURN)});
  abc::TryRegion region; region.offset = {10}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {18}; handler.length = 4;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 4, false, output, {}, 1, !different,
      different ? std::optional{ExpectedCompletion{0, 0}} : std::optional<ExpectedCompletion>{});
}
void AwaitCompletion(abc::ProfileId profile, std::ostream* output) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  // Full compiler-style private-state owner, one await and resolve/reject tail.
  // Normalization supplies facts, never source text; Emit uses the common tree.
  std::vector<std::uint8_t> bytes{
      op(abc::Opcode::kASYNCFUNCTIONENTER), op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3,
      op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
      op(abc::Opcode::kSUSPENDGENERATOR_V8), 0,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRESUMEGENERATOR),
      op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), 0,
      op(abc::Opcode::kGETRESUMEMODE), op(abc::Opcode::kSTA_V8), 2,
      op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(CoroutineResumeMode::kThrow), 0, 0, 0,
      op(abc::Opcode::kEQ_IMM8_V8), 0, 2, op(abc::Opcode::kJEQZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, op(abc::Opcode::kRETURN)};
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0, op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {3}; region.length = handler_pc - 3; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 5;
  body.tries = {region}; body.handlers = {handler};
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  std::vector<abc::CodeOffset> origins;
  std::vector<AwaitSite> awaits;
  const auto normalized = Take<abc::MethodBody>(ProveAsyncCompletion(body, frame,
      SemanticBudget{}.bytes, &origins, &awaits, profile, ResourceFailureScope::kExcluded));
  test::Check(awaits.size() == 1);
  Emit(normalized, profile, 3, 1, false, output, {}, 0, false, {}, CompositionStage::kSource, 0, awaits);
}
// The pre-promise prefix is admitted by typed semantics (no effect, cannot
// throw, no transfer; copies/constants, or depth-0 stores into the fresh
// environment), not by opcode layout. The erased ASYNCFUNCTIONENTER acc
// definition must reach no read on the normal CFG (dataflow, not layout).
void AsyncPrefixes(abc::ProfileId profile) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  using Bytes = std::vector<std::uint8_t>;
  const auto lda = [&](std::uint8_t r) { return Bytes{op(abc::Opcode::kLDA_V8), r}; };
  const auto store = [&](std::uint8_t depth) { return Bytes{op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), depth}; };
  const Bytes env{op(abc::Opcode::kNEWLEXENV_IMM8), 1};
  const Bytes nop{op(abc::Opcode::kNOP)};
  const Bytes copy{op(abc::Opcode::kMOV_V4_V4), 0x32};  // v2 <- v3 (local <- argument)
  const Bytes heap{op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0};
  const Bytes reload = lda(3);
  const Bytes sta1{op(abc::Opcode::kSTA_V8), 1};
  const Bytes branch{op(abc::Opcode::kJEQZ_IMM8), 2};
  const Bytes jump{op(abc::Opcode::kJMP_IMM8), 2};  // to the next instruction
  struct Case { std::vector<Bytes> prefix; std::vector<Bytes> start; bool admitted; };
  const auto concat = [](std::initializer_list<Bytes> parts) { Bytes out; for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end()); return out; };
  const std::vector<Case> cases{
      {{nop, env, lda(3), store(0)}, {reload}, true},             // captured argument
      {{env, copy, lda(2), store(0)}, {reload}, true},            // copied first
      {{env, lda(3), nop, store(0)}, {reload}, true},             // separated by a NOP
      {{env, lda(3), store(1)}, {reload}, false},                 // outer environment
      {{lda(3), store(0)}, {reload}, false},                      // no fresh environment
      {{env, heap}, {reload}, false},                             // observable prefix effect
      {{env, lda(3), store(0)}, {nop}, false},                    // erased acc read by resolve
      {{env, lda(3), store(0)}, {sta1, reload}, false},           // erased acc copied first
      {{env, lda(3), store(0)}, {branch, reload}, false},         // branch reads the erased acc
      {{env, lda(3), store(0)}, {jump, reload}, true},            // jump, then redefinition at its target
      {{env, lda(3), store(0)}, {jump, sta1, reload}, false}};    // the jump reaches a read first
  for (const auto& c : cases) {
    Bytes bytes;
    for (const auto& part : c.prefix) bytes.insert(bytes.end(), part.begin(), part.end());
    const auto prefix = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), {op(abc::Opcode::kASYNCFUNCTIONENTER), op(abc::Opcode::kSTA_V8), 0});
    for (const auto& part : c.start) bytes.insert(bytes.end(), part.begin(), part.end());
    bytes.insert(bytes.end(), {op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, op(abc::Opcode::kRETURN)});
    const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1,
        op(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0, op(abc::Opcode::kRETURN)});
    auto body = test::Body(bytes);
    abc::TryRegion region; region.offset = {prefix + 3}; region.length = handler_pc - prefix - 3; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 5;
    body.tries = {region}; body.handlers = {handler};
    const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
    std::vector<abc::CodeOffset> origins;
    const auto proved = ProveAsyncCompletion(body, frame, SemanticBudget{}.bytes, &origins, nullptr, profile,
        ResourceFailureScope::kExcluded);
    if (std::holds_alternative<abc::MethodBody>(proved) != c.admitted)
      std::cerr << "async prefix case " << (&c - cases.data()) << " admitted=" << std::holds_alternative<abc::MethodBody>(proved)
                << " error=" << (std::holds_alternative<AsyncCompletionFailure>(proved) ? int(std::get<AsyncCompletionFailure>(proved).kind) : -1) << '\n';
    test::Check(std::holds_alternative<abc::MethodBody>(proved) == c.admitted);
    if (!c.admitted) {
      test::Check(std::get<AsyncCompletionFailure>(proved).kind == FunctionEmitError::kKind);
      continue;
    }
    // The whole prefix survives, in order, ahead of the retained body.
    const auto& normalized = std::get<abc::MethodBody>(proved);
    for (std::size_t i = 0; i < c.prefix.size(); ++i)
      test::Check(normalized.instructions[i].decoded.opcode == body.instructions[i].decoded.opcode);
  }
  (void)concat;
}
enum class SharedInnerCatch { kNone, kRethrow, kCallback, kLoopCallback, kReturn };
void CompilerFinally(abc::ProfileId profile, std::ostream* output,
    CompositionStage stage = CompositionStage::kHoleFinally, std::uint32_t count = 1, bool early_return = false,
    SharedInnerCatch inner = SharedInnerCatch::kNone, std::uint32_t depth = 1, bool protected_loop = false,
    bool nested_transfer = false) {
  // Pinned es2panda TryContext::InitFinalizer, TryStatement::CompileFinally
  // and PandaGen::EmitRethrow: shared cleanup, undefined run guard, Hole vs
  // caught payload. This isolates that lowering, not a full es2abc file.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> unit{
      op(abc::Opcode::kLDUNDEFINED), op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDHOLE), op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kJMP_IMM8), 4,
      op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kSTA_V8), 2,
      op(abc::Opcode::kLDUNDEFINED), op(abc::Opcode::kEQ_IMM8_V8), 0, 2,
      op(abc::Opcode::kJEQZ_IMM8), 8,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kSTA_V8), 2,
      op(abc::Opcode::kLDHOLE), op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kNOTEQ_IMM8_V8), 0, 0,
      op(abc::Opcode::kJEQZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), 2,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kRETURN)};
  // Deliberately not a compiler finally: a protected early return bypasses
  // cleanup. Recovering a finally here would observably add a call.
  const std::vector<std::uint8_t> bypass{op(abc::Opcode::kLDA_V8), 6,
      op(abc::Opcode::kJEQZ_IMM8), 4, op(abc::Opcode::kLDUNDEFINED), op(abc::Opcode::kRETURN)};
  // Repeat the protected callback while it returns true. Both loop exit and
  // callback exceptions must enter the same shared cleanup, exactly once.
  const std::vector<std::uint8_t> backedge{op(abc::Opcode::kJEQZ_IMM8), 4,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-6)};
  const std::vector<std::uint8_t> transfer_loop{
      op(abc::Opcode::kLDA_V8), 6, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 12,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 6,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-12),
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)};
  test::Check(!nested_transfer || (!protected_loop && !early_return && inner == SharedInnerCatch::kNone));
  test::Check(!protected_loop || (!early_return && inner == SharedInnerCatch::kNone));
  const auto extra = early_return ? static_cast<std::uint32_t>(bypass.size()) :
      protected_loop ? static_cast<std::uint32_t>(backedge.size()) :
      nested_transfer ? static_cast<std::uint32_t>(transfer_loop.size()) - 4 : 0U;
  if (nested_transfer) {
    unit.erase(unit.begin() + 3, unit.begin() + 7);
    unit.insert(unit.begin() + 3, transfer_loop.begin(), transfer_loop.end());
  }
  if (protected_loop) unit.insert(unit.begin() + 7, backedge.begin(), backedge.end());
  if (early_return) unit.insert(unit.begin() + 3, bypass.begin(), bypass.end());
  test::Check(!early_return || inner == SharedInnerCatch::kNone);
  std::vector<std::uint8_t> nested;
  std::uint32_t nested_handler_size = 0;
  std::uint32_t nested_stride = 0;
  test::Check(depth != 0 && (depth == 1 || inner == SharedInnerCatch::kRethrow));
  if (inner != SharedInnerCatch::kNone) {
    // CompileTryFinally adds an inner catch/rethrow. CompileTryCatchFinally
    // instead invokes source catch code, whose throw reaches the outer catch.
    switch (inner) {
      case SharedInnerCatch::kRethrow:
        nested = {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)}; break;
      case SharedInnerCatch::kCallback:
      case SharedInnerCatch::kLoopCallback:
        nested = {op(abc::Opcode::kSTA_V8), 2, op(abc::Opcode::kLDA_V8), 6,
                  op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 2};
        if (inner == SharedInnerCatch::kLoopCallback) {
          // Keep the caught value in v2 across repeated callback invocations.
          // The backedge enters after STA so callback results cannot replace it.
          nested.insert(nested.end(), {op(abc::Opcode::kJEQZ_IMM8), 4,
              op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-7)});
        }
        break;
      case SharedInnerCatch::kReturn: nested = {op(abc::Opcode::kRETURN)}; break;
      case SharedInnerCatch::kNone: break;
    }
    nested_handler_size = static_cast<std::uint32_t>(nested.size());
    nested.insert(nested.begin(), {op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(2 + nested.size())});
    nested_stride = static_cast<std::uint32_t>(nested.size());
    const auto first_nested = nested;
    for (std::uint32_t level = 1; level < depth; ++level)
      nested.insert(nested.end(), first_nested.begin(), first_nested.end());
    if (inner == SharedInnerCatch::kReturn) {
      // Keep the outer catch reachable from a call outside the inner try;
      // the early catch return must bypass both this call and the cleanup.
      nested.insert(nested.end(), {op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0});
    }
    unit.insert(unit.begin() + 7, nested.begin(), nested.end());
  }
  std::vector<std::uint8_t> bytes;
  for (std::uint32_t i = 0; i < count; ++i) bytes.insert(bytes.end(), unit.begin(), unit.end() - 1);
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto base = static_cast<std::uint32_t>(i * (unit.size() - 1));
    const auto nested_size = static_cast<std::uint32_t>(nested.size());
    abc::TryRegion region; region.offset = {base + 3}; region.length = 4 + extra + nested_size;
    region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {base + 12 + extra + nested_size}; handler.length = 2;
    if (nested_transfer) {
      abc::TryRegion transfer_region;
      transfer_region.offset = {base + 9}; transfer_region.length = 8;
      transfer_region.first_handler = static_cast<std::uint32_t>(body.handlers.size());
      transfer_region.handler_count = 1;
      abc::CatchHandler transfer_handler;
      transfer_handler.offset = {base + 17}; transfer_handler.length = 2;
      body.tries.push_back(transfer_region); body.handlers.push_back(transfer_handler);
    }
    if (inner != SharedInnerCatch::kNone) {
      for (std::uint32_t level = 0; level < depth; ++level) {
        auto inner_region = region; auto inner_handler = handler;
        inner_region.length = 4 + level * nested_stride;
        inner_region.first_handler = static_cast<std::uint32_t>(body.handlers.size());
        inner_handler.offset = {base + 9 + level * nested_stride}; inner_handler.length = nested_handler_size;
        body.tries.push_back(inner_region); body.handlers.push_back(inner_handler);
      }
    }
    region.first_handler = static_cast<std::uint32_t>(body.handlers.size());
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  const auto callback = inner == SharedInnerCatch::kCallback || inner == SharedInnerCatch::kLoopCallback;
  Emit(body, profile, 3, early_return || callback || nested_transfer ? 4 : 3, false, output, {},
      protected_loop || nested_transfer || inner == SharedInnerCatch::kLoopCallback ? 1 : 0,
      stage != CompositionStage::kHoleSource, {}, stage, count);
}
void FinallyOuterTransfer(abc::ProfileId profile, std::ostream* output, bool stop) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  auto body = test::Body({
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 36,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 18,
      op(abc::Opcode::kLDA_V8), 6, op(abc::Opcode::kJEQZ_IMM8), 12,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? 16 : -24),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-20),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-28),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kRETURN)});
  abc::TryRegion region; region.offset = {16}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {30}; handler.length = 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 6, false, output, {}, 2, true);
}
void FinallyTransfer(abc::ProfileId profile, std::ostream* output, bool stop, bool overrides = false,
    bool different = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), static_cast<std::uint8_t>(overrides ? 26 : 28),
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kJEQZ_IMM8), 12,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(stop ? (overrides ? 12 : 14) : -18),
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-20),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0};
  if (overrides) bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>((stop != different) ? 2 : -28)});
  else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
  bytes.insert(bytes.end(), {
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kRETURN)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {10}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {22}; handler.length = overrides ? 8 : 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 5, false, output, {}, 1, !different,
      different ? std::optional{ExpectedCompletion{0, 0}} : std::optional<ExpectedCompletion>{});
}
void NestedFinally(abc::ProfileId profile, std::ostream* output,
    NestedCompletion completion = NestedCompletion::kNormal) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes;
  const auto pc = [&] { return static_cast<std::uint32_t>(bytes.size()); };
  const auto call = [&](std::uint8_t reg) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), reg, op(abc::Opcode::kCALLARG0_IMM8), 0});
  };
  const auto raise = [&] {
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
  };
  call(2);
  if (completion != NestedCompletion::kNormal) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1});
  abc::TryRegion inner; inner.offset = {0}; inner.length = pc(); inner.handler_count = 1;
  call(3);
  const auto jump = pc(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
  abc::CatchHandler first; first.offset = {pc()};
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0});
  call(3); bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0}); raise();
  first.length = pc() - first.offset.value;
  bytes[jump + 1] = static_cast<std::uint8_t>(pc() - jump);
  abc::TryRegion outer = inner; outer.length = pc(); outer.first_handler = 1;
  call(4);
  if (completion == NestedCompletion::kNormal) call(5);
  else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1});
  if (completion == NestedCompletion::kThrow) raise();
  else bytes.push_back(op(abc::Opcode::kRETURN));
  abc::CatchHandler second; second.offset = {pc()};
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0});
  call(4); bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0}); raise();
  second.length = pc() - second.offset.value;
  auto body = test::Body(bytes);
  body.tries = {inner, outer}; body.handlers = {first, second};
  Emit(body, profile, 2, 4, false, output, {}, 0, true, {}, CompositionStage::kSource, 2);
}
void FinallyFallthrough(abc::ProfileId profile, std::ostream* output, bool branch = false,
    bool conditional_cleanup = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0};
  if (conditional_cleanup) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 4,
      op(abc::Opcode::kJEQZ_IMM8), 11});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0});
  if (branch && !conditional_cleanup) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 4,
      op(abc::Opcode::kJEQZ_IMM8), 7});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kRETURN)});
  if (branch) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 5,
      op(abc::Opcode::kCALLARG0_IMM8), 0, op(abc::Opcode::kRETURN)});
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {0}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, branch ? 5 : 3, false, output, {}, 0, !conditional_cleanup,
      conditional_cleanup ? std::optional{ExpectedCompletion{0, 0}} : std::optional<ExpectedCompletion>{});
}
void FinallyOverride(abc::ProfileId profile, bool raising, std::ostream* output,
    OverrideValue value = OverrideValue::kCleanup) {
  const bool original_payload = value == OverrideValue::kCaught;
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0};
  const auto cleanup = [&](bool caught) {
    if (value == OverrideValue::kUndefined && !raising) {
      bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
      return;
    }
    if (value == OverrideValue::kUndefined) bytes.push_back(op(abc::Opcode::kLDUNDEFINED));
    else if (value == OverrideValue::kArgument) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2});
    else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0});
    if (caught) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0});
    if (raising) bytes.insert(bytes.end(), {static_cast<std::uint8_t>(kThrow),
        static_cast<std::uint8_t>(kThrow >> kByteBits)});
    else bytes.push_back(op(abc::Opcode::kRETURN));
  };
  cleanup(false);
  const auto catch_pc = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0});
  cleanup(original_payload);
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {0}; region.length = 4; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {catch_pc}; handler.length = static_cast<std::uint32_t>(bytes.size()) - catch_pc;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 2, false, output, {}, 0, !original_payload,
      original_payload ? std::optional{ExpectedCompletion{0, 0}} : std::optional<ExpectedCompletion>{});
}
void FinallyCatch(abc::ProfileId profile, std::ostream* output, bool early = false) {
  // Inner catch consumes inside's exception; handle/after failures still reach
  // the outer pending-exception cleanup. No sidecar source enters the engine.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), 9,
      op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 1,
      op(abc::Opcode::kLDA_V8), 6, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)};
  if (early) {
    // A return from the inner handler bypasses outer cleanup in this input.
    // Its child-region summary must prevent folding the outer candidate.
    bytes[11] = 10;
    bytes.insert(bytes.begin() + 19, op(abc::Opcode::kRETURN));
  }
  auto body = test::Body(bytes);
  const std::uint32_t extra = early ? 1 : 0;
  abc::TryRegion inner; inner.offset = {6}; inner.length = 4; inner.handler_count = 1;
  abc::TryRegion outer; outer.offset = {0}; outer.length = 23 + extra; outer.first_handler = 1; outer.handler_count = 1;
  abc::CatchHandler handled; handled.offset = {12}; handled.length = 7 + extra;
  abc::CatchHandler cleanup; cleanup.offset = {30 + extra}; cleanup.length = 10;
  body.tries = {inner, outer}; body.handlers = {handled, cleanup};
  Emit(body, profile, 2, 5, false, output, {}, 0, !early, ExpectedCompletion{early ? 0U : 1U, 1});
}
void FinallyLoop(abc::ProfileId profile, std::ostream* output, bool carried = false) {
  // try { const pending = get(); while (next()) step(); return pending; }
  // finally { cleanup(); } lowered into normal/exception cleanup corridors.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJEQZ_IMM8), 8,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-10),
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)};
  if (carried) {
    // x = step(x) feeds the loop phi. The final pending return is the last
    // successful value, not the initial get() result or a failed call result.
    bytes[14] = op(abc::Opcode::kCALLARG1_IMM8_V8);
    bytes.insert(bytes.begin() + 16, {0, op(abc::Opcode::kSTA_V8), 0});
    bytes[11] = 11;
    bytes[20] = static_cast<std::uint8_t>(-13);
  }
  auto body = test::Body(bytes);
  const std::uint32_t prefix = carried ? 3 : 0;
  abc::TryRegion region; region.offset = {0}; region.length = 18 + prefix; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {25 + prefix}; handler.length = 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, 4, false, output, {}, 1, true,
      carried ? std::optional<ExpectedCompletion>{} : std::optional{ExpectedCompletion{1, 1}});
}
void FinallyBranch(abc::ProfileId profile, std::ostream* output,
    ProtectedBranch shape = ProtectedBranch::kSingle) {
  // get's value remains pending while an optional protected call executes.
  // The branch arm rejoins the try exit; it is not an unowned control sink.
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes = {
      op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kJEQZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), 4, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 5, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN),
      op(abc::Opcode::kSTA_V8), 0,
      op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kCALLARG0_IMM8), 0,
      op(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)};
  const bool nested = shape == ProtectedBranch::kNested;
  const bool early = shape == ProtectedBranch::kEarlyReturn;
  if (nested) {
    // Both conditions skip to the same protected continuation. The inner arm
    // inherits the outer join, with no node.next of its own.
    bytes[9] = 10;
    bytes.insert(bytes.begin() + 10, {op(abc::Opcode::kLDA_V8), 6, op(abc::Opcode::kJEQZ_IMM8), 6});
  } else if (early) {
    // This return bypasses cleanup in the original bytecode. It must not be
    // mistaken for an implicit join and placed inside a recovered finalizer.
    bytes[9] = 7;
    bytes.insert(bytes.begin() + 14, op(abc::Opcode::kRETURN));
  }
  auto body = test::Body(bytes);
  const std::uint32_t prefix = nested ? 4 : early ? 1 : 0;
  abc::TryRegion region; region.offset = {0}; region.length = 18 + prefix; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {25 + prefix}; handler.length = 10;
  body.tries = {region}; body.handlers = {handler};
  Emit(body, profile, 1, nested ? 6 : 5, false, output, {}, 0, !early,
      ExpectedCompletion{early ? 0U : 1U, 1});
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--effectful-decision") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool select_false : {false, true})
        Emit(test::SharedDecisionBody(2, select_false, test::DecisionInput::kCall, true), profile, 0, 4, false, nullptr,
            {}, 0, false, ExpectedCompletion{0, 0}, CompositionStage::kSource, 1, {}, true);
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool select_false : {false, true})
        Emit(test::SharedStagedDecisionBody(select_false), profile, 1, 4, true, nullptr,
            {}, 0, false, ExpectedCompletion{0, 0}, CompositionStage::kSource, 1, {}, true);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--snapshot-decision") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto kind : {test::DecisionInput::kCall, test::DecisionInput::kLess}) {
        std::array<std::uint64_t, 4> previous{};
        for (const std::uint32_t count : {2U, 200U, 201U, 400U})
          for (const bool select_false : {false, true}) for (const bool caught : {false, true}) {
            auto body = test::SharedDecisionBody(count, select_false, kind);
            if (caught) {
              const auto length = body.instructions.back().offset.value + body.instructions.back().decoded.size;
              auto returned = test::Body({static_cast<std::uint8_t>(abc::Opcode::kRETURN)}).instructions.front();
              returned.offset = {length};
              body.instructions.push_back(returned);
              abc::TryRegion region; region.offset = {0}; region.length = length; region.handler_count = 1;
              abc::CatchHandler handler; handler.offset = {length}; handler.length = 1;
              body.tries = {region}; body.handlers = {handler};
            }
            const auto variant = static_cast<std::size_t>(select_false) * 2 + static_cast<std::size_t>(caught);
            std::uint64_t work = 0;
            Emit(body, profile, 0, 4, false, &std::cout, {}, 0, false, ExpectedCompletion{0, 0},
                CompositionStage::kSource, 1, {}, false, &work);
            if (count == 201) test::Check(work < previous[variant] * 2);
            if (count == 400) test::Check(work < previous[variant] * 3);
            previous[variant] = work;
          }
      }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--shared-decision") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const std::uint32_t count : {2U, 200U, 201U, 400U}) for (const bool select_false : {false, true})
        Emit(test::SharedDecisionBody(count, select_false), profile, 0, 4, false, &std::cout,
            {}, 0, false, ExpectedCompletion{0, 0});
    std::cout << "];\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--shared-exit") {
    std::cout << "export const shared = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      Emit(test::SharedExitBody(), profile, 0, 7, false, &std::cout,
          {}, 1, false, ExpectedCompletion{0, 0});
    std::cout << "];\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-expanded") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, &std::cout, CompositionStage::kHoleSource);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-import") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, nullptr, CompositionStage::kHoleImport);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-bare-transfer") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyBareTransfer(profile, &std::cout, false);
      FinallyBareTransfer(profile, &std::cout, true);
      FinallyBareTransfer(profile, &std::cout, false, true);
      FinallyBareTransfer(profile, &std::cout, true, true);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) CompilerFinally(profile, &std::cout);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-chain") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      for (const auto count : {2U, 3U}) CompilerFinally(profile, &std::cout, CompositionStage::kHoleFinally, count);
    std::cout << "];\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-nested") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      for (const auto inner : {SharedInnerCatch::kRethrow, SharedInnerCatch::kCallback})
        CompilerFinally(profile, &std::cout, CompositionStage::kHoleFinally, 1, false, inner);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-catch-return") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, &std::cout, CompositionStage::kHoleSource, 1, false, SharedInnerCatch::kReturn);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-deep-plan") {
    for (const auto depth : {200U, 201U, 402U})
      CompilerFinally(abc::ProfileId::kDynamic1206, nullptr, CompositionStage::kHolePlan, 1, false,
          SharedInnerCatch::kRethrow, depth);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--await-completion") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) AwaitCompletion(profile, &std::cout);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-nested-transfer") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, &std::cout, CompositionStage::kHoleFinally, 1, false,
          SharedInnerCatch::kNone, 1, false, true);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-catch-loop") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, &std::cout, CompositionStage::kHoleFinally, 1, false,
          SharedInnerCatch::kLoopCallback);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-loop") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, &std::cout, CompositionStage::kHoleFinally, 1, false,
          SharedInnerCatch::kNone, 1, true);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-depth-source") {
    std::cout << "export const functions = [\n";
    for (const auto depth : {4U, 8U, 16U, 200U, 201U, 402U})
      CompilerFinally(abc::ProfileId::kDynamic1206, &std::cout, CompositionStage::kHoleFinally, 1, false,
          SharedInnerCatch::kRethrow, depth);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--compiler-finally-early-return") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      CompilerFinally(profile, &std::cout, CompositionStage::kHoleSource, 1, true);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-outer-transfer") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyOuterTransfer(profile, &std::cout, false);
      FinallyOuterTransfer(profile, &std::cout, true);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-transfer") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyTransfer(profile, &std::cout, false);
      FinallyTransfer(profile, &std::cout, true);
      FinallyTransfer(profile, &std::cout, false, true);
      FinallyTransfer(profile, &std::cout, true, true);
      FinallyTransfer(profile, &std::cout, false, true, true);
      FinallyTransfer(profile, &std::cout, true, true, true);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--nested-finally") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      NestedFinally(profile, &std::cout);
      NestedFinally(profile, &std::cout, NestedCompletion::kReturn);
      NestedFinally(profile, &std::cout, NestedCompletion::kThrow);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-fallthrough") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyFallthrough(profile, &std::cout);
      FinallyFallthrough(profile, &std::cout, true);
      FinallyFallthrough(profile, &std::cout, true, true);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-override") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyOverride(profile, false, &std::cout);
      FinallyOverride(profile, false, &std::cout, OverrideValue::kCaught);
      FinallyOverride(profile, false, &std::cout, OverrideValue::kUndefined);
      FinallyOverride(profile, false, &std::cout, OverrideValue::kArgument);
      FinallyOverride(profile, true, &std::cout);
      FinallyOverride(profile, true, &std::cout, OverrideValue::kCaught);
      FinallyOverride(profile, true, &std::cout, OverrideValue::kUndefined);
      FinallyOverride(profile, true, &std::cout, OverrideValue::kArgument);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-catch") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyCatch(profile, &std::cout);
      FinallyCatch(profile, &std::cout, true);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-loop") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyLoop(profile, &std::cout);
      FinallyLoop(profile, &std::cout, true);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-branch") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyBranch(profile, &std::cout);
      FinallyBranch(profile, &std::cout, ProtectedBranch::kNested);
      FinallyBranch(profile, &std::cout, ProtectedBranch::kEarlyReturn);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--completion-pairs") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      FinallyChain(profile, &std::cout);
      for (const auto variant : {CleanupVariant::kDifferentCallee, CleanupVariant::kLateReturn,
          CleanupVariant::kExtraCall, CleanupVariant::kWrongRethrow}) FinallyReturn(profile, false, &std::cout, variant);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--finally-return") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      FinallyReturn(profile, true, &std::cout);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--nested-outer") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      NestedOuterTransfer(profile, false, &std::cout);
      NestedOuterTransfer(profile, true, &std::cout);
      HandlerOuterTransfer(profile, false, &std::cout);
      HandlerOuterTransfer(profile, true, &std::cout);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--outer-ports") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) CatchOuterPorts(profile, &std::cout);
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--outer-carried") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      CatchOuterCarried(profile, false, &std::cout);
      CatchOuterCarried(profile, true, &std::cout);
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--outer-transfer" || std::string_view(argv[1]) == "--outer-break")) {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      if (std::string_view(argv[1]) == "--outer-transfer") CatchOuterTransfer(profile, false, &std::cout);
      CatchOuterTransfer(profile, true, &std::cout);
    }
    std::cout << "];\n";
    return 0;
  }
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const functions = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    LoopCarried(profile, emit ? &std::cout : nullptr);
    RetryCarried(profile, emit ? &std::cout : nullptr);
    IterationCell(profile, emit ? &std::cout : nullptr);
    CatchCell(profile, emit ? &std::cout : nullptr);
    CatchOuterTransfer(profile, false, nullptr);
    CatchOuterTransfer(profile, true, nullptr);
    CatchOuterCarried(profile, false, nullptr);
    CatchOuterCarried(profile, true, nullptr);
    CatchOuterPorts(profile, nullptr);
    NestedOuterTransfer(profile, false, nullptr);
    NestedOuterTransfer(profile, true, nullptr);
    HandlerOuterTransfer(profile, false, nullptr);
    HandlerOuterTransfer(profile, true, nullptr);
    FinallyReturn(profile, true, nullptr);
    FinallyChain(profile, nullptr);
    FinallyBranch(profile, nullptr);
    FinallyBranch(profile, nullptr, ProtectedBranch::kNested);
    FinallyBranch(profile, nullptr, ProtectedBranch::kEarlyReturn);
    FinallyLoop(profile, nullptr);
    FinallyLoop(profile, nullptr, true);
    FinallyCatch(profile, nullptr);
    FinallyCatch(profile, nullptr, true);
    FinallyFallthrough(profile, nullptr);
    FinallyFallthrough(profile, nullptr, true);
    FinallyFallthrough(profile, nullptr, true, true);
    NestedFinally(profile, nullptr);
    NestedFinally(profile, nullptr, NestedCompletion::kReturn);
    NestedFinally(profile, nullptr, NestedCompletion::kThrow);
    FinallyTransfer(profile, nullptr, false);
    FinallyTransfer(profile, nullptr, true);
    FinallyTransfer(profile, nullptr, false, true);
    FinallyTransfer(profile, nullptr, true, true);
    FinallyTransfer(profile, nullptr, false, true, true);
    FinallyTransfer(profile, nullptr, true, true, true);
    FinallyBareTransfer(profile, nullptr, false);
    FinallyBareTransfer(profile, nullptr, true);
    FinallyBareTransfer(profile, nullptr, false, true);
    FinallyBareTransfer(profile, nullptr, true, true);
    CompilerFinally(profile, nullptr, CompositionStage::kHoleImport);
    CompilerFinally(profile, nullptr, CompositionStage::kHoleSource);
    CompilerFinally(profile, nullptr, CompositionStage::kHoleFinally);
    AwaitCompletion(profile, nullptr);
    AsyncPrefixes(profile);
    CompilerFinally(profile, nullptr, CompositionStage::kHoleFinally, 1, false,
        SharedInnerCatch::kNone, 1, true);
    CompilerFinally(profile, nullptr, CompositionStage::kHoleFinally, 1, false,
        SharedInnerCatch::kNone, 1, false, true);
    for (const auto inner : {SharedInnerCatch::kRethrow, SharedInnerCatch::kCallback, SharedInnerCatch::kLoopCallback})
      CompilerFinally(profile, nullptr, CompositionStage::kHoleFinally, 1, false, inner);
    CompilerFinally(profile, nullptr, CompositionStage::kHoleSource, 1, false, SharedInnerCatch::kReturn);
    FinallyOuterTransfer(profile, nullptr, false);
    FinallyOuterTransfer(profile, nullptr, true);
    FinallyOverride(profile, false, nullptr);
    FinallyOverride(profile, false, nullptr, OverrideValue::kCaught);
    FinallyOverride(profile, false, nullptr, OverrideValue::kUndefined);
    FinallyOverride(profile, false, nullptr, OverrideValue::kArgument);
    FinallyOverride(profile, true, nullptr);
    FinallyOverride(profile, true, nullptr, OverrideValue::kCaught);
    FinallyOverride(profile, true, nullptr, OverrideValue::kUndefined);
    FinallyOverride(profile, true, nullptr, OverrideValue::kArgument);
    for (const auto variant : {CleanupVariant::kDifferentCallee, CleanupVariant::kLateReturn,
        CleanupVariant::kExtraCall, CleanupVariant::kWrongRethrow}) FinallyReturn(profile, false, nullptr, variant);
  }
  if (emit) std::cout << "];\n";
  if (!emit) for (const auto count : {200U, 201U, 402U})
    FinallyChain(abc::ProfileId::kDynamic1206, nullptr, count, CompositionStage::kCompletion);
  if (!emit) for (const auto depth : {200U, 201U, 402U})
    CompilerFinally(abc::ProfileId::kDynamic1206, nullptr, CompositionStage::kHolePlan, 1, false,
        SharedInnerCatch::kRethrow, depth);
}
