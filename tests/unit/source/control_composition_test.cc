#include "../../../src/source/exception_tree.h"
#include "../../../src/source/completion_plan.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include "abcd/analysis/exception_locals.h"

#include <sstream>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
struct ExpectedCompletion { std::size_t regions, operations; };
enum class CompositionStage { kSource, kCompletion };
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
    CompositionStage stage = CompositionStage::kSource);

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
    CompositionStage stage) {
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(locals, parameters, {}));
  const auto flow = Take<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
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
  const auto proof = Take<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, environments, values));
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  const auto graph = Take<ValueProgram>(ImportPhasedValues(input));
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
  if (const auto* error = std::get_if<EvaluationError>(&planned))
    std::cerr << "composition evaluation error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  const auto completions = Take<CompletionPlan>(BuildCompletionPlan(graph, regions, control, evaluation));
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
  else if (!evaluation.cells && expected_loops == 1) {
    test::Check(evaluation.phis.has_value());
    bool empty_schedule = false;
    for (const auto& bundle : evaluation.phis->bundles)
      empty_schedule |= bundle.schedule.steps.empty();
    test::Check(empty_schedule); // Regression: no orphan origin for erased self-copies.
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  FunctionSyntax syntax{};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"};
  syntax.parameters.resize(frame.parameters);
  if (expect_finalizer) {
    // Reserve an existing spelling. Source parameter names currently exclude
    // the '$' namespace, but recovery must not assume every input name does.
    syntax.names = {u"$completion0"};
  }
  const ExpressionConstants resolved{constants, static_cast<std::uint32_t>(syntax.names.size()), 1, TextId{0}};
  const ExceptionTreeInput context{body, input, regions, control};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  if (const auto* error = std::get_if<FunctionTreeError>(&built))
    std::cerr << "loop-carried tree error " << static_cast<unsigned>(*error) << '\n';
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  if (expect_finalizer) {
    std::size_t finalizers = 0;
    for (const auto& statement : tree.statements)
      if (const auto* region = std::get_if<TryStmt>(&statement.statement); region && region->finalizer)
        ++finalizers;
    if (finalizers != 1) std::cerr << "missing recovered finalizer: " << finalizers << '\n';
    test::Check(finalizers == 1);
    test::Check(tree.finalizers.size() == 1);
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
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, tree)));
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
      graph, context, evaluation, tree, resolved)));
  const auto family = Take<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
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
}
int main(int argc, char** argv) {
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
    for (const auto variant : {CleanupVariant::kDifferentCallee, CleanupVariant::kLateReturn,
        CleanupVariant::kExtraCall, CleanupVariant::kWrongRethrow}) FinallyReturn(profile, false, nullptr, variant);
  }
  if (emit) std::cout << "];\n";
  if (!emit) for (const auto count : {200U, 201U, 402U})
    FinallyChain(abc::ProfileId::kDynamic1206, nullptr, count, CompositionStage::kCompletion);
}
