#include "../../../src/source/exception_copies.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_plan.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/binding_plan.h"
#include "../../../src/source/expression_forest.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Append(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  auto& value = graph.values[std::get<ValueId>(use.operand).value];
  use.next = value.first_use;
  if (use.next) graph.uses[use.next->value].previous = id;
  value.first_use = id;
  graph.uses.push_back(use);
}
ValueProgram ReadAndCatch(std::uint32_t count) {
  ValueProgram graph;
  graph.blocks = {{{0}, 1, {0}, 2}, {{1}, 0, {2}, 1},
      {{1}, 1, {3}, 1}, {{2}, 0, {4}, 0},
      {{2}, 1, {4}, 1}, {{3}, 0, {5}, 0}, {{3}, 0, {5}, 1}};
  graph.phases = {{{0}, {0}, ExceptionPhase::kBefore, OpId{0}},
      {{1}, {0}, ExceptionPhase::kCommit, OpId{0}},
      {{2}, {1}, ExceptionPhase::kBefore, OpId{1}},
      {{3}, {1}, ExceptionPhase::kCommit, OpId{1}},
      {{4}, {2}, ExceptionPhase::kBefore, OpId{2}},
      {{5}, {2}, ExceptionPhase::kCommit, OpId{2}},
      {{6}, {2}, ExceptionPhase::kCatch, {}}};
  graph.edges = {{{0}, {1}, FlowEdgeKind::kFallthrough, 0},
      {{0}, {6}, FlowEdgeKind::kException, 1},
      {{1}, {2}, FlowEdgeKind::kFallthrough, 0},
      {{2}, {3}, FlowEdgeKind::kFallthrough, 0},
      {{4}, {5}, FlowEdgeKind::kFallthrough, 0},
      {{6}, {4}, FlowEdgeKind::kFallthrough, 0}};
  graph.origins = {{{0}, {0}, {}}, {{1}, {4}, {}}, {{2}, {5}, {}}};
  graph.values = {{EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}},
      {OpResult{{0}, WorkBlockId{1}}, ValueKinds::Language(), {}},
      {CatchDefinition{{6}}, ValueKinds::Language(), {}}};
  for (std::uint32_t i = 0; i < count; ++i)
    graph.values.push_back({PhiDefinition{{6}}, ValueKinds::Language(), {}});
  constexpr auto kReadEffects = LanguageEffect::kHeapRead | LanguageEffect::kUserCode;
  graph.ops = {{ValueNamedRead{{0}}, {0}, 0, {0}, 1, ValueId{1},
      kReadEffects, kReadEffects, ExceptionalCompletion::kMayThrow, {0}},
      {ValueReturn{}, {2}, 0, {1}, 1, {}, LanguageEffect::kNone,
      LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}},
      {ValueReturn{}, {4}, 0, {2}, 1, {}, LanguageEffect::kNone,
      LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {2}}};
  Append(graph, {OpId{0}, OperandRole::kBase, ReadPurpose::kOperand, ValueId{0}, {}, {}, {}, {}});
  Append(graph, {OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand, ValueId{1}, {}, {}, {}, {}});
  Append(graph, {OpId{2}, OperandRole::kReturnValue, ReadPurpose::kOperand,
      ValueId{count ? 3U : 2U}, {}, {}, {}, {}});
  for (std::uint32_t i = 0; i < count; ++i)
    Append(graph, {ValueId{i + 3}, OperandRole::kPhiInput, ReadPurpose::kOperand,
        ValueId{0}, IncomingKey{EdgeId{1}}, {}, {}, {}});
  return graph;
}
void Staging(std::uint32_t count) {
  auto graph = ReadAndCatch(count);
  const auto built = BuildPhasedPhiCopies(graph);
  test::Check(std::holds_alternative<PhiCopies>(built));
  const auto& copies = std::get<PhiCopies>(built);
  test::Check(copies.flow == PhiFlow::kPhased);
  test::Check(std::holds_alternative<PhiCopies>(BuildPhasedPhiCopies(
      graph, {SemanticBudget{}.bytes, copies.work})));
  test::Check(std::get<PhiCopyError>(BuildPhasedPhiCopies(
      graph, {SemanticBudget{}.bytes, copies.work - 1})) == PhiCopyError::kWorkLimit);
  test::Check(std::get<PhiCopyError>(BuildPhiCopies(graph)) == PhiCopyError::kFlow);
  test::Check(std::get<PhiCopyError>(VerifyPhiCopies(graph, copies)) == PhiCopyError::kFlow);
  auto planned = BuildExceptionCopies(graph, copies);
  test::Check(std::holds_alternative<ExceptionCopies>(planned));
  const auto& plan = std::get<ExceptionCopies>(planned);
  test::Check(plan.slots.size() == count && plan.staging.size() == count);
  if (count) {
    test::Check(plan.commits.size() == 1 && plan.commits[0].count == count);
    const auto before = std::get<ThrowCopies>(plan.placements[0]);
    test::Check(before.operation == OpId{0} && before.handler == WorkBlockId{6});
    // Execute only the permitted pre-throw writes. Existing phi locations must
    // remain untouched when the getter returns, including across repeated calls.
    constexpr std::uint32_t kOldPhi = 17, kInput = 29, kNextInput = 41;
    std::vector<std::uint32_t> locations(graph.values.size(), kOldPhi);
    locations[0] = kInput;
    std::vector<std::uint32_t> staged(count);
    const auto stage = [&] {
      for (std::size_t i = 0; i < copies.copies.size(); ++i)
        staged[plan.staging[i]->value] = locations[std::get<ValueId>(copies.copies[i].source).value];
    };
    stage();
    for (const auto& slot : plan.slots) test::Check(locations[slot.destination.value] == kOldPhi);
    locations[0] = kNextInput;
    stage();
    for (const auto& commit : plan.commits)
      for (std::uint32_t i = 0; i < commit.count; ++i) {
        const auto slot = commit.first.value + i;
        locations[plan.slots[slot].destination.value] = staged[slot];
      }
    for (const auto& slot : plan.slots) test::Check(locations[slot.destination.value] == kNextInput);
    auto changed = plan;
    changed.placements[0] = NormalCopies{{1}};
    test::Check(std::get<ExceptionCopyError>(VerifyExceptionCopies(graph, copies, changed)) ==
        ExceptionCopyError::kPlacement);
    changed = plan; changed.staging[0].reset();
    test::Check(std::get<ExceptionCopyError>(VerifyExceptionCopies(graph, copies, changed)) ==
        ExceptionCopyError::kSlot);
    changed = plan; changed.slots[0].destination = {1};
    test::Check(std::get<ExceptionCopyError>(VerifyExceptionCopies(graph, copies, changed)) ==
        ExceptionCopyError::kSlot);
    changed = plan; changed.commits.clear();
    test::Check(std::get<ExceptionCopyError>(VerifyExceptionCopies(graph, copies, changed)) ==
        ExceptionCopyError::kCommit);
    auto unavailable = graph;
    test::Check(!ReplaceUse(unavailable, {3}, ValueId{1}));
    test::Check(std::get<PhiCopyError>(BuildPhasedPhiCopies(unavailable)) == PhiCopyError::kFlow);
  }
  const auto verified = VerifyExceptionCopies(graph, copies, plan);
  test::Check(std::holds_alternative<std::uint64_t>(verified));
  const auto work = std::get<std::uint64_t>(verified);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyExceptionCopies(
      graph, copies, plan, {SemanticBudget{}.bytes, work})));
  test::Check(std::holds_alternative<ExceptionCopyError>(VerifyExceptionCopies(
      graph, copies, plan, {SemanticBudget{}.bytes, work - 1})));
  test::Check(std::get<ExceptionCopyError>(BuildExceptionCopies(graph, copies, {0})) ==
      ExceptionCopyError::kResourceLimit);
  test::Check(std::holds_alternative<ExceptionCopies>(BuildExceptionCopies(
      graph, copies, {SemanticBudget{}.bytes, plan.work})));
  test::Check(std::get<ExceptionCopyError>(BuildExceptionCopies(
      graph, copies, {SemanticBudget{}.bytes, plan.work - 1})) == ExceptionCopyError::kWorkLimit);
  const auto evaluated = BuildPhasedEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  test::Check(evaluation.flow == EvaluationFlow::kPhased);
  test::Check(evaluation.exceptions.has_value() == (count != 0));
  test::Check(evaluation.operations[0].kind == EvaluationKind::kRoot);
  test::Check(evaluation.operations[0].temporary == TemporaryReason::kCrossRegionValue);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  test::Check(std::holds_alternative<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work})));
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(
      graph, {SemanticBudget{}.bytes, evaluation.work - 1})) == EvaluationError::kWorkLimit);
  test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(graph, {0})) ==
      EvaluationError::kResourceLimit);
  test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)) ==
      EvaluationError::kUnsupportedOperation);
  test::Check(std::holds_alternative<BindingError>(BuildBindingPlan(graph, evaluation, 1)));
  const auto bound = BuildPhasedBindingPlan(graph, evaluation, 1);
  test::Check(std::holds_alternative<BindingPlan>(bound));
  const auto& bindings = std::get<BindingPlan>(bound);
  test::Check(std::holds_alternative<BindingPlan>(BuildPhasedBindingPlan(
      graph, evaluation, 1, {SemanticBudget{}.bytes, bindings.work})));
  test::Check(std::get<BindingError>(BuildPhasedBindingPlan(
      graph, evaluation, 1, {SemanticBudget{}.bytes, bindings.work - 1})) == BindingError::kWorkLimit);
  test::Check(bindings.flow == EvaluationFlow::kPhased && bindings.catch_slots.size() == count);
  const auto caught_symbol = std::get<SymbolId>(bindings.values[2]);
  const auto caught = std::get<CatchParameterBinding>(bindings.symbols[caught_symbol.value]);
  test::Check(caught.value == ValueId{2} && caught.block == WorkBlockId{6});
  std::vector<bool> slot_symbols(bindings.symbols.size());
  for (std::size_t s = 0; s < bindings.catch_slots.size(); ++s) {
    const auto symbol = bindings.catch_slots[s];
    test::Check(!slot_symbols[symbol.value]); slot_symbols[symbol.value] = true;
    test::Check(std::get<CatchSlotBinding>(bindings.symbols[symbol.value]).slot.value == s);
  }
  for (const auto& value : bindings.values)
    if (const auto* symbol = std::get_if<SymbolId>(&value)) test::Check(!slot_symbols[symbol->value]);
  const auto called = BuildPhasedCallPlan(graph, evaluation);
  test::Check(std::holds_alternative<CallPlan>(called));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedCallPlan(
      graph, evaluation, std::get<CallPlan>(called))));
  test::Check(std::get<CallPlanError>(VerifyCallPlan(graph, evaluation, std::get<CallPlan>(called))) ==
      CallPlanError::kEvaluation);
  const std::vector<ExpressionConstant> constants{NameId{0}, std::monostate{}, std::monostate{}};
  const auto expressed = BuildPhasedExpressionForest(graph, evaluation, 1, {0}, {constants, 1});
  test::Check(std::holds_alternative<ExpressionForest>(expressed));
  const auto& forest = std::get<ExpressionForest>(expressed);
  test::Check(std::holds_alternative<ExpressionForest>(BuildPhasedExpressionForest(
      graph, evaluation, 1, {0}, {constants, 1}, {SemanticBudget{}.bytes, forest.work})));
  test::Check(std::get<ExpressionError>(BuildPhasedExpressionForest(
      graph, evaluation, 1, {0}, {constants, 1}, {SemanticBudget{}.bytes, forest.work - 1})) ==
      ExpressionError::kWorkLimit);
  test::Check(std::get<ExpressionError>(BuildPhasedExpressionForest(
      graph, evaluation, 1, {0}, {constants, 1}, {0})) == ExpressionError::kResourceLimit);
  test::Check(forest.edges.empty() && forest.edge_assignments.empty());
  test::Check(forest.throw_assignments.size() == count && forest.catch_assignments.size() == count);
  test::Check(std::holds_alternative<ExpressionError>(BuildExpressionForest(
      graph, evaluation, 1, {0}, {constants, 1})));
  std::vector<bool> owned(forest.expressions.size());
  for (const auto roots : {std::span<const ExprId>(forest.throw_assignments),
                           std::span<const ExprId>(forest.catch_assignments)}) {
    for (const auto root : roots) {
      test::Check(!owned[root.value]); owned[root.value] = true;
      const auto rhs = std::get<AssignmentExpr>(forest.expressions[root.value].expression).value;
      test::Check(!owned[rhs.value]); owned[rhs.value] = true;
    }
  }
  for (const auto root : forest.operations) if (root) test::Check(!owned[root->value]);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& staged = forest.expressions[forest.throw_assignments[i].value];
    const auto& saved = std::get<AssignmentExpr>(staged.expression);
    const auto slot = forest.bindings.catch_slots[plan.staging[i]->value];
    test::Check(std::get<SymbolTarget>(saved.target).symbol == slot);
    test::Check(std::get<SymbolExpr>(forest.expressions[saved.value.value].expression).symbol == SymbolId{0});
    test::Check(std::get<EdgeOrigin>(forest.origins[staged.origin.value]).edge == EdgeId{1});
    const auto& committed = forest.expressions[forest.catch_assignments[i].value];
    const auto& assigned = std::get<AssignmentExpr>(committed.expression);
    const auto phi = plan.slots[i].destination;
    test::Check(std::get<SymbolTarget>(assigned.target).symbol == std::get<SymbolId>(forest.bindings.values[phi.value]));
    test::Check(std::get<SymbolExpr>(forest.expressions[assigned.value.value].expression).symbol == slot);
    test::Check(std::get<EdgeOrigin>(forest.origins[committed.origin.value]).edge == EdgeId{5});
  }
  if (count) {
    auto missing = evaluation; missing.exceptions.reset();
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, missing)));
    auto forged = evaluation;
    forged.exceptions->placements[0] = NormalCopies{{1}};
    test::Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph, forged)));
    auto unavailable = graph;
    test::Check(!ReplaceUse(unavailable, {2}, ValueId{1}));
    test::Check(std::get<EvaluationError>(BuildPhasedEvaluationPlan(unavailable)) ==
        EvaluationError::kUnavailableValue);
  }
}
enum class ThrowingSite : std::uint8_t { kGetter, kCall };
void Decoded(abc::ProfileId profile, ThrowingSite site) {
  // The first getter/call can throw with old r0. The second can throw after r0
  // was updated. Both exceptions enter the same catch, but carry different r0.
  auto body_bytes = std::vector<std::uint8_t>{
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 2};
  const auto append_site = [&] {
    switch (site) {
      case ThrowingSite::kGetter:
        body_bytes.insert(body_bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0});
        break;
      case ThrowingSite::kCall:
        body_bytes.insert(body_bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kCALLARG0_IMM8), 0});
        break;
    }
  };
  const abc::CodeOffset first_pc{static_cast<std::uint32_t>(body_bytes.size())};
  append_site();
  body_bytes.insert(body_bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 3});
  const abc::CodeOffset second_pc{static_cast<std::uint32_t>(body_bytes.size())};
  append_site();
  body_bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
  const abc::CodeOffset handler_pc{static_cast<std::uint32_t>(body_bytes.size())};
  body_bytes.insert(body_bytes.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  auto body = test::Body(body_bytes);
  abc::TryRegion region;
  region.offset = first_pc; region.length = handler_pc.value - first_pc.value; region.handler_count = 1;
  body.tries.push_back(region);
  abc::CatchHandler handler;
  handler.offset = handler_pc; handler.length = 3;
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 3, {}));
  auto built = BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame);
  const auto& flow = std::get<ExceptionFlow>(built);
  IdfQuota quota{kDefaultSemanticWork};
  auto renamed = ExceptionSsa::Build(flow, frame.registers, quota);
  const auto& ssa = std::get<ExceptionSsa>(renamed);
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (std::holds_alternative<MethodEntry>(value.origin) && reg && reg->value >= frame.locals)
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  }
  auto bound = AnalyzeExceptionEnvironments(flow);
  const auto& environments = std::get<EnvironmentFlow>(bound);
  IdfQuota value_quota{kDefaultSemanticWork};
  auto propagated = ExceptionValueFlow::Build(flow, ssa, entries, value_quota,
      {}, PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage);
  const auto& values = std::get<ExceptionValueFlow>(propagated);
  auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
  const auto& proof = std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  auto imported = ImportPhasedValues(input);
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(std::holds_alternative<ExceptionPlan>(BuildExceptionPlan(body, graph, input)));
  const auto copied = BuildPhasedPhiCopies(graph);
  test::Check(std::holds_alternative<PhiCopies>(copied));
  const auto& copies = std::get<PhiCopies>(copied);
  const auto planned = BuildExceptionCopies(graph, copies);
  test::Check(std::holds_alternative<ExceptionCopies>(planned));
  const auto& plan = std::get<ExceptionCopies>(planned);
  test::Check(plan.slots.size() == 1 && plan.commits.size() == 1);
  test::Check(plan.placements.size() == 2 && plan.staging.size() == 2);
  test::Check(plan.staging[0] == plan.staging[1]);
  test::Check(std::get<ValueId>(copies.copies[0].source) == graph.ops[1].result);
  test::Check(std::get<ValueId>(copies.copies[1].source) == graph.ops[4].result);
  test::Check(std::get<ThrowCopies>(plan.placements[0]).operation == OpId{3});
  test::Check(std::get<ThrowCopies>(plan.placements[1]).operation == OpId{6});
  const auto evaluated = BuildPhasedEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
  const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  test::Check(evaluation.exceptions && evaluation.exceptions->slots.size() == 1);
  test::Check(evaluation.operations[3].kind == EvaluationKind::kRoot &&
      evaluation.operations[6].kind == EvaluationKind::kRoot);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation)));
  std::vector<ExpressionConstant> constants(graph.ops.size());
  if (site == ThrowingSite::kGetter) constants[3] = constants[6] = NameId{0};
  const auto expressed = BuildPhasedExpressionForest(graph, evaluation, 3, {0}, {constants, 1});
  test::Check(std::holds_alternative<ExpressionForest>(expressed));
  const auto& forest = std::get<ExpressionForest>(expressed);
  test::Check(forest.throws.size() == 2 && forest.catches.size() == 1);
  test::Check(forest.throw_assignments.size() == 2 && forest.catch_assignments.size() == 1);
  const auto& first = forest.expressions[forest.operations[3]->value];
  const auto& second = forest.expressions[forest.operations[6]->value];
  test::Check(std::get<OriginalSite>(forest.origins[first.origin.value]).pc == first_pc);
  test::Check(std::get<OriginalSite>(forest.origins[second.origin.value]).pc == second_pc);
  if (site == ThrowingSite::kCall) {
    test::Check(std::get<CallExpr>(first.expression).reference == CallReference::kValue);
    test::Check(std::get<CallExpr>(second.expression).reference == CallReference::kValue);
  } else {
    test::Check(std::holds_alternative<MemberExpr>(first.expression));
    test::Check(std::holds_alternative<MemberExpr>(second.expression));
  }
}
void MixedPlacements() {
  auto graph = ReadAndCatch(1);
  graph.values.push_back({PhiDefinition{{0}}, ValueKinds::Language(), {}});
  graph.values.push_back({PhiDefinition{{2}}, ValueKinds::Language(), {}});
  graph.values.push_back({PhiDefinition{{4}}, ValueKinds::Language(), {}});
  Append(graph, {ValueId{4}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{0}, IncomingKey{MethodEntryKey{}}, {}, {}, {}});
  Append(graph, {ValueId{5}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{1}, IncomingKey{EdgeId{2}}, {}, {}, {}});
  Append(graph, {ValueId{6}, OperandRole::kPhiInput, ReadPurpose::kOperand,
      ValueId{2}, IncomingKey{EdgeId{5}}, {}, {}, {}});
  auto built = BuildPhasedPhiCopies(graph);
  test::Check(std::holds_alternative<PhiCopies>(built));
  const auto& copies = std::get<PhiCopies>(built);
  auto planned = BuildExceptionCopies(graph, copies);
  test::Check(std::holds_alternative<ExceptionCopies>(planned));
  const auto& plan = std::get<ExceptionCopies>(planned);
  test::Check(plan.placements.size() == 4 && plan.slots.size() == 1);
  test::Check(std::holds_alternative<EntryCopies>(plan.placements[0]));
  test::Check(std::holds_alternative<ThrowCopies>(plan.placements[1]));
  test::Check(std::get<NormalCopies>(plan.placements[2]).edge == EdgeId{2});
  test::Check(std::get<NormalCopies>(plan.placements[3]).edge == EdgeId{5});
  test::Check(!plan.staging[0] && plan.staging[1] && !plan.staging[2] && !plan.staging[3]);
  auto changed = plan; changed.staging[3] = CatchSlotId{0};
  test::Check(std::get<ExceptionCopyError>(VerifyExceptionCopies(graph, copies, changed)) ==
      ExceptionCopyError::kSlot);
  changed = plan; ++changed.cfg_revision.value;
  test::Check(std::get<ExceptionCopyError>(VerifyExceptionCopies(graph, copies, changed)) ==
      ExceptionCopyError::kRevision);
  auto wrong = copies; wrong.flow = PhiFlow::kNormal;
  test::Check(std::get<PhiCopyError>(VerifyPhasedPhiCopies(graph, wrong)) == PhiCopyError::kFlow);
}
}  // namespace
int main() {
  for (const std::uint32_t count : {0U, 1U, 2U, 200U, 201U, 402U}) Staging(count);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    Decoded(profile, ThrowingSite::kGetter);
    Decoded(profile, ThrowingSite::kCall);
  }
  MixedPlacements();
}
