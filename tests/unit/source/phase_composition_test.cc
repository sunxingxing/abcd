#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/phase_evaluation.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/source_variables.h"
#include "../../../src/source/flow_motion.h"
#include "analysis/ssa_fixture.h"
#include "abcd/analysis/arithmetic_semantics.h"

// Phased composition across a committed selection (call-references.md §5.4).
// Base: try { r = obj.f(1 + (c ? x : y)); } catch { return; } return r;
// Each variant breaks exactly one obligation of the shared order proof.
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Variant {
  kBase, kForeignBefore, kForeignAfterJoin, kCallOutsideTry, kCatchCopies, kUniformCopies,
  kExtraUse,  // The callee value is observed again after the call.
  kSelectionInput,  // One retained getter feeds both predicate and selected arm.
  kSelectionCopy,  // Equivalent register copies of that retained input.
  kSelectionForeign,  // Its evaluation is separated by an unrelated getter.
  kSelectionEscape,  // The input is also observed outside the selection.
  kSelectionValue,  // Already complete value call; preserve its original snapshots.
  kChain,     // A chain of Binaries over the selection, each split by a foreign getter.
  kPureChain  // A run of pure (non-event) typeof wrappers over the selection's Binary.
};
void BoundaryCounts() {
  for (const bool repeated_stage : {false, true}) {
    RecoveryFlow flow; flow.events.resize(5); flow.entry = {0}; flow.terminal = {4};
    const auto edge = [&](FlowPoint from, FlowPoint to) {
      const auto id = flow.edges.size();
      flow.edges.push_back({from, to, FlowCompletion::kNormal,
          flow.events[to.value].incoming, flow.events[from.value].outgoing});
      flow.events[to.value].incoming = flow.events[from.value].outgoing = id;
    };
    edge({0}, {1}); edge({1}, {2}); edge({2}, {3});
    edge({3}, repeated_stage ? FlowPoint{1} : FlowPoint{2}); edge({3}, {4});
    const std::array<FlowPoint, 2> boundaries{{{1}, {2}}};
    const auto motion = std::get<FlowMotion>(AnalyzeFlowBoundaryMotion(flow, boundaries));
    test::Check(motion.next[1] == FlowPoint{2});
    // A one-time stage still dominates a loop's repeated expression. Only
    // the universal previous boundary proves one stage per evaluation.
    test::Check((motion.previous[2] == FlowPoint{1}) == repeated_stage);
    test::Check(std::holds_alternative<FlowMotion>(AnalyzeFlowBoundaryMotion(
        flow, boundaries, {motion.allocated_bytes, motion.work})));
    test::Check(std::get<TreeStructureError>(AnalyzeFlowBoundaryMotion(
        flow, boundaries, {motion.allocated_bytes, motion.work - 1})) == TreeStructureError::kWorkLimit);
  }
}
// Returns the planner's work, for linear-scaling checks.
std::uint64_t Run(abc::ProfileId profile, Variant variant, std::uint32_t chain = 0) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr std::uint8_t kCallee = 0, kLeft = 1, kArgument = 2, kResult = 3;
  constexpr std::uint8_t kSnapshot = 4;
  constexpr std::uint8_t kObject = 8, kCondition = 9, kX = 10, kY = 11;
  // Prelude outside the try: the handler may read kLeft on every path.
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kLDAI_IMM32), 0, 0, 0, 0, op(abc::Opcode::kSTA_V8), kLeft};
  const auto try_begin = static_cast<std::uint32_t>(bytes.size());
  if (variant == Variant::kUniformCopies) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0, op(abc::Opcode::kSTA_V8), kLeft});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), kCallee});
  if (variant != Variant::kUniformCopies)
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0, op(abc::Opcode::kSTA_V8), kLeft});
  // A getter whose result is discarded: an ordered event outside the call tree.
  const auto foreign = [&] {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kObject, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
  };
  if (variant == Variant::kForeignBefore) foreign();
  const bool selection_input = variant == Variant::kSelectionInput || variant == Variant::kSelectionCopy ||
      variant == Variant::kSelectionForeign || variant == Variant::kSelectionEscape ||
      variant == Variant::kSelectionValue;
  if (selection_input) {
    foreign();
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kSnapshot});
    if (variant == Variant::kSelectionCopy)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kSnapshot, op(abc::Opcode::kSTA_V8), kArgument,
          op(abc::Opcode::kLDA_V8), kArgument, op(abc::Opcode::kSTA_V8), kSnapshot});
    if (variant == Variant::kSelectionForeign) foreign();
  }
  constexpr auto kIsFalse = static_cast<std::uint16_t>(abc::Opcode::kCALLRUNTIME_ISFALSE_PREF_IMM8);
  constexpr unsigned kByteBits = 8;
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), selection_input ? kSnapshot : kCondition, static_cast<std::uint8_t>(kIsFalse),
      static_cast<std::uint8_t>(kIsFalse >> kByteBits), 0, op(abc::Opcode::kJNEZ_IMM8), 6,
      op(abc::Opcode::kLDA_V8), selection_input ? kSnapshot : kX, op(abc::Opcode::kJMP_IMM8), 4,
      op(abc::Opcode::kLDA_V8), kY});
  if (variant == Variant::kForeignAfterJoin) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
    foreign();
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kADD2_IMM8_V8), 0, kLeft});
  for (std::uint32_t i = 0; variant == Variant::kChain && i < chain; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
    foreign();
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kArgument, op(abc::Opcode::kADD2_IMM8_V8), 0, kLeft});
  }
  if (variant == Variant::kPureChain || variant == Variant::kUniformCopies || variant == Variant::kSelectionInput ||
      variant == Variant::kSelectionValue)
    for (std::uint32_t i = 0; i < chain; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kTYPEOF_IMM8), 0});
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kArgument});
  const auto before_call = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee});
  if (variant == Variant::kSelectionValue)
    bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARG1_IMM8_V8), 0, kArgument});
  else bytes.insert(bytes.end(), {op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, kObject, kArgument});
  bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), kResult});
  if (variant == Variant::kExtraUse)
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kCallee, op(abc::Opcode::kSTA_V8), kResult});
  if (variant == Variant::kSelectionEscape)
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kSnapshot, op(abc::Opcode::kSTA_V8), kResult});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kResult, op(abc::Opcode::kRETURN)});
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  // Reading a register assigned inside the span gives its throw points catch copies.
  if ((variant == Variant::kCatchCopies || variant == Variant::kUniformCopies)) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), kLeft});
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {try_begin}; region.length = handler_pc - try_begin; region.handler_count = 1;
  if (variant == Variant::kCallOutsideTry) region.length = before_call - try_begin;
  abc::CatchHandler handler; handler.offset = {handler_pc};
  handler.length = (variant == Variant::kCatchCopies || variant == Variant::kUniformCopies) ? 3 : 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(5, 7, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, ssa_work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage,
      FrameReceiver::kUnknown, ArithmeticSemantics::kSourceNumeric));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  std::optional<OpId> callee, call;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (!callee && std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) callee = OpId{static_cast<std::uint32_t>(i)};
    if (std::holds_alternative<ValueInvoke>(graph.ops[i].operation)) call = OpId{static_cast<std::uint32_t>(i)};
  }
  test::Check(callee && call);
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject));
  if (variant == Variant::kUniformCopies) callee = MemberProperty(graph, evaluation, *call);
  test::Check(callee.has_value());
  const bool composed = variant == Variant::kBase || variant == Variant::kPureChain ||
      variant == Variant::kSelectionInput || variant == Variant::kSelectionCopy || variant == Variant::kUniformCopies;
  const bool stable = variant == Variant::kSelectionValue;
  if (stable) test::Check(evaluation.phase_selections[0].prefix.empty());
  if (variant == Variant::kBase || variant == Variant::kPureChain)
    test::Check(evaluation.phase_prefix_domain == PhasePrefixDomain::kPredicate);
  // A foreign event after the join, or catch staging of a span event, may
  // already keep the selection itself uncommitted; nothing is crossed then.
  test::Check(!evaluation.phase_selections.empty() || variant == Variant::kForeignAfterJoin ||
      variant == Variant::kCatchCopies || variant == Variant::kSelectionForeign);
  test::Check(evaluation.phase_compositions.empty() != composed);
  test::Check((evaluation.operations[callee->value].kind == EvaluationKind::kInline) == composed);
  test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, evaluation)) == (composed || stable));
  const auto checked = std::get<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation));
  test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(
      graph, evaluation, {SemanticBudget{}.bytes, checked - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work}, IncomingCells::kReject)));
  const auto less_work = BuildPhasedEvaluationPlan(graph, {SemanticBudget{}.bytes, evaluation.work - 1}, IncomingCells::kReject);
  test::Check(std::holds_alternative<EvaluationFailure>(less_work));
  test::Check(std::get<EvaluationFailure>(less_work) == EvaluationError::kWorkLimit);
  const auto rejected = [&](const EvaluationPlan& forged) {
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
  };
  if (stable) {
    test::Check(!NeedsMemberReference(graph, evaluation, *call));
    test::Check(evaluation.phase_prefix_domain == PhasePrefixDomain::kPredicate);
    auto forged = evaluation;
    forged.phase_prefix_domain = PhasePrefixDomain::kSelection;
    rejected(forged);  // An expanded certificate cannot borrow legacy prefixes.
    return evaluation.work;
  }
  test::Check(NeedsMemberReference(graph, evaluation, *call));
  auto protocol = evaluation;
  protocol.operations[call->value].kind = EvaluationKind::kForOfEvent;
  test::Check(!NeedsMemberReference(graph, protocol, *call));
  if (!composed) {
    const auto failure = std::get<CallPlanFailure>(BuildPhasedCallPlan(graph, evaluation));
    test::Check(failure == CallPlanError::kReference &&
        std::get<CallReferenceError>(failure.cause) == CallReferenceError::kNotInline);
    // The builder's refusal is not a verifier gap: claiming the composition fails.
    auto forged = evaluation;
    forged.operations[callee->value] = {EvaluationKind::kInline, graph.ops[call->value].first_use, {}};
    rejected(forged);
    forged.phase_compositions.push_back(*call);
    rejected(forged);
    return evaluation.work;
  }
  if (variant == Variant::kUniformCopies) {
    test::Check(evaluation.exception_stage_domain == ExceptionStageDomain::kUniform);
    test::Check(!evaluation.exception_emission.groups.empty() && !evaluation.exception_emission.coverage.empty());
    auto changed = evaluation;
    changed.exception_emission.assignments[0].source = SpecialValue::kNull;
    test::Check(std::get<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, changed)) == EvaluationError::kCatchState);
    changed = evaluation;
    ++changed.exception_emission.assignments[0].slot.value;
    rejected(changed);
    changed = evaluation;
    changed.exception_emission.groups[0].entry = *call;
    rejected(changed);
    changed = evaluation;
    changed.exception_emission.coverage.pop_back();
    rejected(changed);
    changed = evaluation;
    changed.exception_emission.bundles[changed.exception_emission.coverage[0].bundle].reset();
    rejected(changed);
    if (!chain) {
      const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
      const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
      const ExceptionTreeInput context{body, input, regions, control};
      std::vector<ExpressionConstant> constants(graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i)
        if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) constants[i] = NameId{0};
      FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction;
      syntax.strictness = Strictness::kStrict; syntax.names = {u"field"};
      syntax.parameters.resize(frame.parameters);
      const auto draft = std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(
          graph, context, evaluation, syntax, {constants, 1}));
      const auto semantics = std::get<TreeSemantics>(VerifyExceptionTreeSemantics(
          graph, context, evaluation, draft.tree, {constants, 1}));
      const auto& tree_flow = semantics.flow;
      test::Check(!tree_flow.shared_stages.empty());
      const auto stage = tree_flow.shared_stages.front();
      auto tree = draft.tree;
      auto& assignment = std::get<AssignmentExpr>(tree.expressions[stage.value].expression);
      const auto rhs = assignment.value;
      tree.expressions[rhs.value].expression = SymbolExpr{tree.parameters.back().symbol};
      test::Check(std::get<TreeSemanticError>(VerifyExceptionTreeSemantics(
          graph, context, evaluation, tree, {constants, 1})) == TreeSemanticError::kOperand);
      tree = draft.tree;
      const auto commit = tree_flow.catch_commits.front().front();
      std::get<AssignmentExpr>(tree.expressions[stage.value].expression).target =
          std::get<AssignmentExpr>(tree.expressions[commit.value].expression).target;
      test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, tree)));
      tree = draft.tree;
      bool moved = false;
      for (const auto& node : draft.tree.statements) {
        const auto* block = std::get_if<BlockStmt>(&node.statement); if (!block) continue;
        for (std::uint32_t i = 0; i + 1 < block->child_count; ++i) {
          const auto at = block->first_child + i;
          const auto* expression = std::get_if<ExpressionStmt>(&tree.statements[tree.children[at].value].statement);
          if (!expression || expression->expression != stage) continue;
          std::swap(tree.children[at], tree.children[at + 1]); moved = true;
        }
      }
      test::Check(moved && std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, tree)));
      test::Check(std::holds_alternative<SourceVariables>(PrepareSourceVariables(draft.tree, {}, tree_flow.original.get())));
      auto original = *tree_flow.original;
      original.exception_stages.front().entry = *call;
      test::Check(std::holds_alternative<TreeStructureError>(PrepareSourceVariables(draft.tree, {}, &original)));
      original = *tree_flow.original;
      original.exception_stages.front().handler = graph.ops[call->value].block;
      test::Check(std::holds_alternative<TreeStructureError>(PrepareSourceVariables(draft.tree, {}, &original)));
    }
  }
  // The representative is a summary, not permission to discard any original
  // exception source. Move each original throw to a different destination.
  for (std::size_t e = 0; e < graph.exceptional_exits.size(); ++e) {
    if (!graph.exceptional_exits[e].handler || graph.exceptional_exits[e].op.value > call->value) continue;
    if (variant == Variant::kUniformCopies && std::none_of(evaluation.exception_emission.coverage.begin(),
        evaluation.exception_emission.coverage.end(), [&](const auto& cover) { return cover.operation == graph.exceptional_exits[e].op; })) continue;
    auto changed_graph = graph;
    changed_graph.exceptional_exits[e].handler.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(changed_graph, evaluation)));
  }
  test::Check(evaluation.phase_compositions.size() == 1 && evaluation.phase_compositions[0] == *call);
  for (const auto& selection : evaluation.phase_selections) test::Check(selection.execution == *call);
  auto forged = evaluation;
  forged.phase_compositions.clear();  // Inline callee without its composition proof.
  rejected(forged);
  forged = evaluation;
  forged.phase_compositions.push_back(*call);  // Duplicate root.
  rejected(forged);
  forged = evaluation;
  forged.phase_compositions.insert(forged.phase_compositions.begin(), *callee);  // Not a root.
  rejected(forged);
  forged = evaluation;
  forged.phase_selections[0].execution = *callee;  // Stale execution owner.
  rejected(forged);
  forged = evaluation;
  forged.operations[callee->value] = {EvaluationKind::kRoot, {}, {}};  // Snapshot claimed composed.
  rejected(forged);
  if (variant == Variant::kSelectionInput || variant == Variant::kSelectionCopy) {
    test::Check(evaluation.phase_prefix_domain == PhasePrefixDomain::kSelection);
    forged = evaluation;
    forged.phase_prefix_domain = PhasePrefixDomain::kPredicate;
    rejected(forged);  // Each retained input needs its actual enclosure proof.
    test::Check(evaluation.phase_selections[0].prefix.size() == 1);
    forged = evaluation;
    const auto prefix = forged.phase_selections[0].prefix[0];
    forged.phase_selections[0].prefix.clear();
    forged.phase_prefixes[prefix.value].reset();  // Omitted retained getter event.
    rejected(forged);
  }
  // Byte accounting is deterministic and bounded: find the smallest grant and
  // check that one byte less fails cleanly with a resource limit.
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low + 1 < high) {
    const auto middle = low + (high - low) / 2;
    if (std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(graph, {middle, evaluation.work},
        IncomingCells::kReject))) high = middle; else low = middle;
  }
  const auto less_bytes = BuildPhasedEvaluationPlan(graph, {high - 1, evaluation.work}, IncomingCells::kReject);
  test::Check(std::holds_alternative<EvaluationFailure>(less_bytes));
  test::Check(std::get<EvaluationFailure>(less_bytes) == EvaluationError::kResourceLimit);
  return evaluation.work;
}
}  // namespace
int main() {
  BoundaryCounts();
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301})
    for (const auto variant : {Variant::kBase, Variant::kForeignBefore, Variant::kForeignAfterJoin,
                               Variant::kCallOutsideTry, Variant::kCatchCopies, Variant::kUniformCopies, Variant::kExtraUse,
                               Variant::kSelectionInput, Variant::kSelectionCopy, Variant::kSelectionForeign,
                               Variant::kSelectionEscape, Variant::kSelectionValue})
      Run(profile, variant);
  // A failing chain must not be re-expanded per candidate: linear planner work.
  // A run of pure wrappers is not rescanned per consumer either.
  for (const auto variant : {Variant::kChain, Variant::kPureChain, Variant::kSelectionInput,
                            Variant::kSelectionValue, Variant::kUniformCopies}) {
    std::uint64_t previous = 0;
    for (const auto count : {200U, 201U, 400U, 800U}) {
      const auto work = Run(abc::ProfileId::kDynamic1206, variant, count);
      if (count == 400 || count == 800) test::Check(work < 3 * previous);
      previous = work;
    }
  }
}
