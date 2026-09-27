#include "../../../src/source/function_input.h"
#include "../../../src/source/capture_cells.h"
#include "../../../src/source/capture_evaluation.h"
#include "../../../src/source/capture_tree.h"
#include "../../../src/source/capture_aliases.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/function_family.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/cell_defaults.h"
#include "../../../src/source/cell_default_proof.h"
#include "../../../src/source/default_signature.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/source/value_stage.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>
#include <type_traits>
#include <limits>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
static_assert(!std::is_copy_constructible_v<FunctionInput>);
static_assert(!std::is_default_constructible_v<CellDefaultProof>);
static_assert(!std::is_copy_constructible_v<CellDefaultProof>);
static_assert(!std::is_constructible_v<FunctionInput, SemanticMethod, SemanticProof>);
static_assert(std::is_same_v<decltype(std::declval<FunctionInput&>().Proof()), const SemanticProof&>);
constexpr abc::EntityOffset kParent{200}, kChild{300}, kRecord{100};
constexpr std::uint8_t kPush = 0x09, kLoad = 0x60, kStoreCell = 0x3d,
    kReadCell = 0x3c, kPop = 0x69, kFunction = 0x33, kReturn = 0x64, kReturnUndefined = 0x65;
abc::FrameLayout Frame() {
  return std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
}
RouteInputIdentity Identity() {
  RouteInputIdentity result;
  result.profile = abc::ProfileId::kDynamic1206;
  result.abc_version = {12, 0, 6, 0};
  return result;
}
FunctionInput Input(abc::EntityOffset method, const abc::MethodBody& body) {
  return std::get<FunctionInput>(FunctionInput::Build(
      Identity(), method, kRecord, body, Frame(), InputCaptureMode::kConditional));
}
abc::MetadataIndex Index() {
  abc::MetadataIndex index;
  index.methods.resize(2);
  index.methods[0].offset = kParent; index.methods[1].offset = kChild;
  for (auto& method : index.methods) method.owner_offset = kRecord;
  return index;
}
CreationBinding Creation(const FunctionInput& parent, InstructionId site) {
  // Unit-level resolved binding fixture. File/pool resolution is covered by
  // creation_binding_test and creation_tree_context_test, not invented here.
  const auto& instruction = parent.Input().instructions[site.value];
  return {instruction.pc, std::get<FunctionCreation>(instruction.operation),
      {{1}, {abc::FunctionKind::kBaseConstructor, false, 0, {}}},
      OrdinaryCapture{*parent.Proof().environments.before[site.value]}};
}

void TreeBinding(const FunctionValues& parent, const FunctionValues& child,
    const EvaluationPlan& evaluation, const CaptureCells& cells) {
  const auto parent_control = std::get<ControlPlan>(BuildControlPlan(parent.graph));
  std::vector<ExpressionConstant> parent_constants(parent.graph.ops.size());
  FunctionSyntax parent_syntax;
  parent_syntax.identity = parent.identity; parent_syntax.method = parent.method;
  parent_syntax.owner = {0}; parent_syntax.callable = CallableKind::kFunction;
  parent_syntax.strictness = Strictness::kStrict; parent_syntax.parameters.resize(Frame().parameters);
  auto outer = std::get<ConstructedFunction>(BuildFunctionTree(parent.graph, parent_control,
      evaluation, parent_syntax, {parent_constants, 0, 0})).tree;
  const auto parent_flow = std::get<TreeFlow>(VerifyTreeFlow(parent.graph, parent_control, evaluation, outer));
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(parent.graph,
      parent_control, evaluation, outer, {parent_constants, 0, 0})));
  FunctionSyntax child_syntax;
  child_syntax.identity = child.identity; child_syntax.method = child.method;
  child_syntax.owner = {1}; child_syntax.callable = CallableKind::kFunction;
  child_syntax.strictness = Strictness::kStrict; child_syntax.parameters.resize(Frame().parameters);
  child_syntax.names.push_back(u"reservedName");
  const IncomingCaptureTree incoming{cells, {outer, parent_flow}};
  const auto child_control = std::get<ControlPlan>(BuildControlPlan(child.graph));
  const auto child_evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(child.graph, {}, IncomingCells::kConditional));
  std::vector<ExpressionConstant> child_constants(child.graph.ops.size());
  const ExpressionConstants constants{child_constants, child_syntax.names.size(), 0};
  auto inner = std::get<ConstructedFunction>(BuildFunctionTree(child.graph, child_control,
      child_evaluation, child_syntax, constants, {}, &incoming)).tree;
  const auto child_flow = std::get<TreeFlow>(VerifyTreeFlow(child.graph, child_control, child_evaluation, inner));
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(child.graph,
      child_control, child_evaluation, inner, constants)));
  const auto verify = [&](const FunctionTree& candidate) {
    return VerifyCaptureTree(cells, {outer, parent_flow}, {candidate, child_flow});
  };
  const auto work = std::get<std::uint64_t>(verify(inner));
  // Production construction, not this fixture, now selects actual parent
  // symbols/names and independently checks the resulting child relationship.
  const auto names = std::get<CaptureAliases>(BindCaptureAliases(cells,
      {outer, parent_flow}, inner.owner, {1}));
  test::Check(names.aliases.size() == cells.targets.size());
  for (std::size_t i = 0; i < names.aliases.size(); ++i) {
    const auto& alias = names.aliases[i];
    test::Check(alias.cell == cells.targets[i].child && alias.name == NameId{static_cast<std::uint32_t>(i + 1)});
    test::Check(alias.target == inner.captures[i].target);
    test::Check(names.names[i] == inner.names[inner.symbols[inner.captures[i].symbol.value].name.value]);
  }
  SemanticBudget naming_budget; naming_budget.work = names.work;
  test::Check(std::holds_alternative<CaptureAliases>(BindCaptureAliases(cells, {outer, parent_flow}, inner.owner, {1}, naming_budget)));
  --naming_budget.work;
  test::Check(std::get<CaptureTreeError>(BindCaptureAliases(cells, {outer, parent_flow}, inner.owner, {1}, naming_budget)) == CaptureTreeError::kWorkLimit);
  naming_budget = {}; naming_budget.bytes = 0;
  test::Check(std::get<CaptureTreeError>(BindCaptureAliases(cells, {outer, parent_flow}, inner.owner, {1}, naming_budget)) == CaptureTreeError::kResourceLimit);
  auto wrong_owner = child_syntax; wrong_owner.owner = {2};
  test::Check(std::get<FunctionTreeError>(BuildFunctionTree(child.graph, child_control,
      child_evaluation, wrong_owner, constants, {}, &incoming)) == FunctionTreeError::kCapture);
  auto stale = cells; ++stale.child_revision.value;
  const IncomingCaptureTree stale_input{stale, {outer, parent_flow}};
  test::Check(std::get<FunctionTreeError>(BuildFunctionTree(child.graph, child_control,
      child_evaluation, child_syntax, constants, {}, &stale_input)) == FunctionTreeError::kCapture);
  if (!names.aliases.empty()) {
    auto supplied = child_syntax; supplied.captures = names.aliases;
    test::Check(std::get<FunctionTreeError>(BuildFunctionTree(child.graph, child_control,
        child_evaluation, supplied, constants, {}, &incoming)) == FunctionTreeError::kCapture);
    test::Check(std::get<CaptureTreeError>(BindCaptureAliases(cells, {outer, parent_flow}, inner.owner,
        {std::numeric_limits<std::uint32_t>::max()})) == CaptureTreeError::kResourceLimit);
    auto outward = cells; outward.targets[0].target = OuterCell{{{0}, {0}}};
    test::Check(std::get<CaptureTreeError>(BindCaptureAliases(outward, {outer, parent_flow}, inner.owner)) == CaptureTreeError::kOuterCapture);
    auto bad_flow = parent_flow;
    bad_flow.cells[std::get<ParentCell>(cells.targets[0].target).cell.value].reset();
    test::Check(std::get<CaptureTreeError>(BindCaptureAliases(cells, {outer, bad_flow}, inner.owner)) == CaptureTreeError::kCell);
  }
  if (evaluation.functions.size() == 1) {
    // Reverse storage order deliberately: owners are not vector indices, and
    // family traversal must still place the real parent before its child.
    std::vector<FamilyMember> members{{inner, child_flow, cells}, {outer, parent_flow, {}}};
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily(members, outer.owner));
    test::Check(family.root == FamilyId{1} && family.order == std::vector<FamilyId>{{1}, {0}});
    test::Check(family.parents[0]->function == FamilyId{1} && !family.parents[1]);
    // Shared printer events must return to the caller's owner after each child.
    // This source draft carries no executable/publication receipt.
    std::ostringstream output;
    const auto printed = std::get<PrintedFamilyDraft>(PrintFamilyDraft(family, output));
    test::Check(printed.members.size() == 2 && printed.bytes == output.str().size());
    test::Check(printed.functions[1].begin == 0);
    test::Check(printed.functions[0].begin > printed.functions[1].begin &&
        printed.functions[0].end < printed.functions[1].end);
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      test::Check(printed.members[i].method == family.members[i].tree.method);
      test::Check(printed.members[i].fragment == FunctionFragment::kConditionalExpression);
      for (const auto& range : printed.members[i].expressions)
        test::Check(range.begin >= printed.functions[i].begin && range.end <= printed.functions[i].end && range.begin < range.end);
    }
    for (std::size_t i = 0; i < outer.expressions.size(); ++i)
      if (std::holds_alternative<FunctionExpr>(outer.expressions[i].expression))
        test::Check(printed.members[1].expressions[i] == printed.functions[0]);
    SemanticBudget print_budget; print_budget.work = printed.work;
    std::ostringstream exact;
    test::Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family, exact, {}, print_budget)));
    --print_budget.work;
    std::ostringstream exhausted;
    test::Check(std::get<TreePrintError>(PrintFamilyDraft(family, exhausted, {}, print_budget)) == TreePrintError::kWorkLimit);
    print_budget = {}; print_budget.bytes = 0;
    test::Check(std::get<TreePrintError>(PrintFamilyDraft(family, exhausted, {}, print_budget)) == TreePrintError::kResourceLimit);
    test::Check(std::get<TreePrintError>(PrintFamilyDraft(family, exhausted, {0})) == TreePrintError::kOutputLimit);
    auto missing = members; missing.erase(missing.begin());
    test::Check(std::get<FamilyError>(BuildFunctionFamily(std::move(missing), outer.owner)) == FamilyError::kMissingChild);
    auto duplicate = members; duplicate.push_back(members[0]);
    test::Check(std::get<FamilyError>(BuildFunctionFamily(std::move(duplicate), outer.owner)) == FamilyError::kOwner);
    auto unbound = members; unbound[0].incoming.reset();
    test::Check(std::get<FamilyError>(BuildFunctionFamily(std::move(unbound), outer.owner)) == FamilyError::kCapture);
    SemanticBudget family_budget; family_budget.work = family.work;
    test::Check(std::holds_alternative<ConstructedFamily>(BuildFunctionFamily(members, outer.owner, family_budget)));
    --family_budget.work;
    test::Check(std::get<FamilyError>(BuildFunctionFamily(members, outer.owner, family_budget)) == FamilyError::kWorkLimit);
    family_budget = {}; family_budget.bytes = 0;
    test::Check(std::get<FamilyError>(BuildFunctionFamily(members, outer.owner, family_budget)) == FamilyError::kResourceLimit);
  }
  SemanticBudget budget; budget.work = work;
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCaptureTree(cells,
      {outer, parent_flow}, {inner, child_flow}, budget)));
  --budget.work;
  test::Check(std::get<CaptureTreeError>(VerifyCaptureTree(cells,
      {outer, parent_flow}, {inner, child_flow}, budget)) == CaptureTreeError::kWorkLimit);
  auto changed = inner; ++changed.value_revision.value;
  test::Check(std::get<CaptureTreeError>(verify(changed)) == CaptureTreeError::kRevision);
  changed = inner; changed.owner = outer.owner;
  test::Check(std::get<CaptureTreeError>(verify(changed)) == CaptureTreeError::kOwner);
  if (inner.captures.empty()) return;
  changed = inner; changed.captures[0].target.owner = {2};
  test::Check(std::get<CaptureTreeError>(verify(changed)) == CaptureTreeError::kTarget);
  changed = inner; changed.names[changed.symbols[changed.captures[0].symbol.value].name.value] += u"other";
  test::Check(std::get<CaptureTreeError>(verify(changed)) == CaptureTreeError::kName);
  changed = inner; changed.captures.pop_back();
  test::Check(std::get<CaptureTreeError>(verify(changed)) == CaptureTreeError::kCoverage);
}

void Occurrences(std::uint32_t count) {
  const auto parent_body = test::Body({kPush, 1, kLoad, 0, kStoreCell, 0,
      kFunction, 0, 0, 0, 1, kPop, kPush, 1, kLoad, 0, kStoreCell, 0,
      kFunction, 0, 0, 0, 1, kPop, kReturnUndefined});
  auto parent = Input(kParent, parent_body);
  std::vector<std::uint8_t> code;
  for (std::uint32_t i = 0; i < count; ++i) code.insert(code.end(), {kReadCell, 0, kStoreCell, 0});
  code.push_back(kReturnUndefined);
  const auto body = test::Body(code);
  const auto child = Input(kChild, body);
  test::Check(child.Captures().size() == 2 * count && child.CaptureAccesses().size() == child.Captures().size());
  test::Check(child.Proof().captured_reads.size() == count && child.Children().empty());
  for (std::size_t i = 0; i < child.Captures().size(); ++i) {
    test::Check(child.Captures()[i].domain == CaptureDomain::kLanguage &&
        child.Captures()[i].cell.depth == CapturedDepth{0} && child.Captures()[i].cell.slot.value == 0);
    test::Check(child.CaptureAccesses()[i].instruction.value == i &&
        child.CaptureAccesses()[i].kind == (i % 2 == 0 ? LocalAccessKind::kLoad : LocalAccessKind::kStore));
  }
  test::Check(parent.Children().size() == 2 && parent.Captures().empty());
  const auto index = Index();
  const auto first_site = parent.Children()[0], second_site = parent.Children()[1];
  const auto first = Creation(parent, first_site), second = Creation(parent, second_site);
  auto parent_values = std::get<FunctionValues>(BuildFunctionValues(parent, parent_body, Frame()));
  auto child_values = std::get<FunctionValues>(BuildFunctionValues(child, body, Frame()));
  const CaptureFunction parent_work{parent, parent_values}, child_work{child, child_values};
  const auto first_cells = std::get<CaptureCells>(BindCaptureCells(index, parent_work, child_work, first_site, first));
  const auto second_cells = std::get<CaptureCells>(BindCaptureCells(index, parent_work, child_work, second_site, second));
  const auto first_binding = std::get<CapturedFunctionBinding>(BindCaptureEvaluation(index,
      parent_work, child_work, first_site, first, first_cells, {0}, {1}));
  const auto second_binding = std::get<CapturedFunctionBinding>(BindCaptureEvaluation(index,
      parent_work, child_work, second_site, second, second_cells, {0}, {2}));
  const FunctionBinding functions[]{first_binding.binding, second_binding.binding};
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(parent_values.graph, {},
      IncomingCells::kConditional, functions));
  TreeBinding(parent_values, child_values, evaluation, first_cells);
  test::Check(evaluation.functions.size() == 2 && first_binding.binding.child != second_binding.binding.child);
  test::Check(first_binding.binding.captures.size() == first_cells.targets.size());
  test::Check(std::get<CaptureEvaluationError>(std::get<CaptureEvaluationFailure>(BindCaptureEvaluation(index,
      parent_work, child_work, first_site, first, first_cells, {1}, {1}))) == CaptureEvaluationError::kOwner);
  SemanticBudget binding_budget; binding_budget.work = first_binding.work;
  test::Check(std::holds_alternative<CapturedFunctionBinding>(BindCaptureEvaluation(index,
      parent_work, child_work, first_site, first, first_cells, {0}, {1}, binding_budget)));
  --binding_budget.work;
  test::Check(std::holds_alternative<CaptureEvaluationFailure>(BindCaptureEvaluation(index,
      parent_work, child_work, first_site, first, first_cells, {0}, {1}, binding_budget)));
  test::Check(first_cells.creation != second_cells.creation && first_cells.targets.size() == (count == 0 ? 0 : 1));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyCaptureCells(index, parent_work, child_work,
      first_site, first, first_cells)));
  if (count != 0) {
    const auto first_cell = std::get<ParentCell>(first_cells.targets[0].target).cell;
    const auto second_cell = std::get<ParentCell>(second_cells.targets[0].target).cell;
    test::Check(first_cell != second_cell);
    test::Check(std::get<AllocatedCell>(parent_values.cells->cells[first_cell.value].storage).allocation !=
        std::get<AllocatedCell>(parent_values.cells->cells[second_cell.value].storage).allocation);
    auto forged = first_cells;
    forged.targets[0].target = ParentCell{second_cell};
    test::Check(std::get<CaptureCellError>(std::get<CaptureEvaluationFailure>(BindCaptureEvaluation(index,
        parent_work, child_work, first_site, first, forged, {0}, {1}))) == CaptureCellError::kBinding);
    test::Check(std::get<CaptureCellError>(VerifyCaptureCells(index, parent_work, child_work,
        first_site, first, forged)) == CaptureCellError::kBinding);
    forged = first_cells; forged.targets.clear();
    test::Check(std::get<CaptureCellError>(VerifyCaptureCells(index, parent_work, child_work,
        first_site, first, forged)) == CaptureCellError::kCoverage);
    forged = first_cells; forged.targets[0].target = OuterCell{{{0}, {0}}};
    test::Check(std::get<CaptureCellError>(VerifyCaptureCells(index, parent_work, child_work,
        first_site, first, forged)) == CaptureCellError::kBinding);
  }
  SemanticBudget cell_budget; cell_budget.work = first_cells.work;
  test::Check(std::holds_alternative<CaptureCells>(BindCaptureCells(index, parent_work, child_work,
      first_site, first, cell_budget)));
  --cell_budget.work;
  test::Check(std::get<CaptureCellError>(BindCaptureCells(index, parent_work, child_work,
      first_site, first, cell_budget)) == CaptureCellError::kWorkLimit);
  cell_budget = {}; cell_budget.bytes = 0;
  test::Check(std::get<CaptureCellError>(BindCaptureCells(index, parent_work, child_work,
      first_site, first, cell_budget)) == CaptureCellError::kResourceLimit);
  ++child_values.graph.revision.value;
  test::Check(std::get<CaptureCellError>(VerifyCaptureCells(index, parent_work, child_work,
      first_site, first, first_cells)) == CaptureCellError::kRevision);
  --child_values.graph.revision.value;
  const auto identity = child_values.identity;
  child_values.identity.file_hash.bytes[0] = 1;
  test::Check(std::get<CaptureCellError>(VerifyCaptureCells(index, parent_work, child_work,
      first_site, first, first_cells)) == CaptureCellError::kOwner);
  child_values.identity = identity;
  const auto bound = std::get<CapturePlan>(BindFunctionInputs(index, parent, first_site, first, child));
  const auto later = std::get<CapturePlan>(BindFunctionInputs(index, parent, second_site, second, child));
  test::Check(bound.bindings.size() == child.Captures().size() && later.bindings.size() == bound.bindings.size());
  if (count != 0) {
    test::Check(std::get<LocalCell>(bound.bindings[0].cell).environment !=
        std::get<LocalCell>(later.bindings[0].cell).environment);
    test::Check(std::get<LocalCell>(bound.bindings[0].cell).environment ==
        std::get<LocalCell>(bound.bindings[1].cell).environment);
  }
  SemanticBudget budget; budget.work = bound.work;
  test::Check(std::holds_alternative<CapturePlan>(BindFunctionInputs(index, parent, first_site, first, child, budget)));
  --budget.work;
  test::Check(std::get<CaptureError>(BindFunctionInputs(index, parent, first_site, first, child, budget)) == CaptureError::kWorkLimit);
  auto wrong = first; ++wrong.offset.value;
  test::Check(std::get<CaptureError>(BindFunctionInputs(index, parent, first_site, wrong, child)) == CaptureError::kCreation);
  auto foreign = Identity(); foreign.file_hash.bytes[0] = 1;
  const auto other = std::get<FunctionInput>(FunctionInput::Build(foreign, kChild, kRecord,
      body, Frame(), InputCaptureMode::kConditional));
  test::Check(std::get<InputBindingError>(BindFunctionInputs(index, parent, first_site, first, other)) == InputBindingError::kIdentity);
  auto changed_index = Index(); changed_index.methods[1].offset = {301};
  test::Check(std::get<InputBindingError>(BindFunctionInputs(changed_index, parent, first_site, first, child)) == InputBindingError::kTarget);
}

void ForwardingAndChildren() {
  const auto parent_body = test::Body({kFunction, 0, 0, 0, 1, kReturnUndefined});
  const auto child_body = test::Body({kReadCell, 0, kStoreCell, 0, kFunction, 0, 0, 0, 1, kReturnUndefined});
  const auto parent = Input(kParent, parent_body);
  const auto child = Input(kChild, child_body);
  const auto site = parent.Children()[0];
  const auto bound = std::get<CapturePlan>(BindFunctionInputs(Index(), parent, site, Creation(parent, site), child));
  test::Check(bound.bindings.size() == 2 && std::holds_alternative<CapturedSlot>(bound.bindings[0].cell));
  test::Check(child.Children().size() == 1 && child.Proof().closure_writes == ClosureWrites::kLanguage);
  // Binding direct demands does not mutate/discharge the child's descendants.
  test::Check(child.Children()[0].value == 2 && child.Captures().size() == 2);
  const auto parent_values = std::get<FunctionValues>(BuildFunctionValues(parent, parent_body, Frame()));
  const auto child_values = std::get<FunctionValues>(BuildFunctionValues(child, child_body, Frame()));
  const auto cells = std::get<CaptureCells>(BindCaptureCells(Index(), {parent, parent_values},
      {child, child_values}, site, Creation(parent, site)));
  test::Check(cells.targets.size() == 1 && std::get<OuterCell>(cells.targets[0].target).cell.depth == CapturedDepth{0});
  test::Check(std::get<CaptureEvaluationError>(std::get<CaptureEvaluationFailure>(BindCaptureEvaluation(Index(),
      {parent, parent_values}, {child, child_values}, parent.Children()[0],
      Creation(parent, parent.Children()[0]), cells, {0}, {1}))) == CaptureEvaluationError::kOuterCapture);
  // The parent now directly accesses that same outer cell. The child must use
  // the parent's incoming binding, without discharging the parent's obligation.
  const auto accessing = Input(kParent, child_body);
  const auto accessing_values = std::get<FunctionValues>(BuildFunctionValues(accessing, child_body, Frame()));
  const auto accessing_site = accessing.Children()[0];
  const auto shared = std::get<CaptureCells>(BindCaptureCells(Index(), {accessing, accessing_values},
      {child, child_values}, accessing_site, Creation(accessing, accessing_site)));
  test::Check(std::get<ParentCell>(shared.targets[0].target).cell == CellId{0} && accessing.Captures().size() == 2);
  const auto uninitialized_body = test::Body({kPush, 1, kFunction, 0, 0, 0, 1, kPop, kReturnUndefined});
  const auto uninitialized = Input(kParent, uninitialized_body);
  const auto uninitialized_values = std::get<FunctionValues>(BuildFunctionValues(uninitialized, uninitialized_body, Frame()));
  const auto uninitialized_site = uninitialized.Children()[0];
  test::Check(std::get<CaptureCellError>(BindCaptureCells(Index(), {uninitialized, uninitialized_values},
      {child, child_values}, uninitialized_site, Creation(uninitialized, uninitialized_site))) == CaptureCellError::kInput);
}

void ClosedAdmission() {
  const auto body = test::Body({kReadCell, 0, kReturn});
  test::Check(std::holds_alternative<FunctionInputFailure>(FunctionInput::Build(
      Identity(), kChild, kRecord, body, Frame(), InputCaptureMode::kClosed)));
  const auto values = BuildValueStage(Identity(), kChild, kRecord, body, Frame());
  test::Check(!values.graph && values.receipt.failure && values.receipt.failure->reason == ValueRouteError::kInputStage);
  const auto plain = test::Body({kLoad, 0, kReturn});
  const auto closed = std::get<FunctionInput>(FunctionInput::Build(
      Identity(), kChild, kRecord, plain, Frame(), InputCaptureMode::kClosed));
  test::Check(closed.Captures().empty() && closed.Children().empty() && closed.Proof().closure_writes == ClosureWrites::kUnknown);
  // A write-only incoming access is still a parent binding obligation.
  const auto writer = Input(kChild, test::Body({kLoad, 0, kStoreCell, 0, kReturnUndefined}));
  test::Check(writer.Captures().size() == 1 && writer.Proof().captured_reads.empty() &&
      writer.CaptureAccesses()[0].kind == LocalAccessKind::kStore);
  SemanticBudget budget; budget.bytes = 0;
  const auto failed = FunctionInput::Build(Identity(), kChild, kRecord, plain, Frame(), InputCaptureMode::kConditional, budget);
  test::Check(std::get<FunctionInputError>(std::get<FunctionInputFailure>(failed).cause) == FunctionInputError::kResourceLimit);
}
void DistinctCells(std::uint8_t count) {
  // v7 wide lexical access has u16 depth and slot operands. These cases keep
  // the high bytes zero and cover 200/201 distinct cells, not repeated slot 0.
  constexpr std::uint8_t kWide = 0xfd, kWideRead = 0x0c, kWideStore = 0x0d;
  std::vector<std::uint8_t> parent_code{kPush, count, kLoad, 0};
  std::vector<std::uint8_t> child_code{kPush, count, kLoad, 0};
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto slot = static_cast<std::uint8_t>(i);
    parent_code.insert(parent_code.end(), {kWide, kWideStore, 0, 0, slot, 0});
    child_code.insert(child_code.end(), {kWide, kWideStore, 0, 0, slot, 0});
  }
  parent_code.insert(parent_code.end(), {kFunction, 0, 0, 0, 1, kPop, kReturnUndefined});
  child_code.push_back(kPop);
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto slot = static_cast<std::uint8_t>(i);
    child_code.insert(child_code.end(), {kWide, kWideRead, 0, 0, slot, 0,
        kWide, kWideStore, 0, 0, slot, 0});
  }
  child_code.push_back(kReturnUndefined);
  const auto parent_body = test::Body(parent_code), child_body = test::Body(child_code);
  const auto parent = Input(kParent, parent_body), child = Input(kChild, child_body);
  const auto parent_values = std::get<FunctionValues>(BuildFunctionValues(parent, parent_body, Frame()));
  const auto child_values = std::get<FunctionValues>(BuildFunctionValues(child, child_body, Frame()));
  const auto site = parent.Children()[0];
  const auto cells = std::get<CaptureCells>(BindCaptureCells(Index(), {parent, parent_values},
      {child, child_values}, site, Creation(parent, site)));
  test::Check(cells.targets.size() == count && child_values.cells->cells.size() == 2 * count);
  const auto binding = std::get<CapturedFunctionBinding>(BindCaptureEvaluation(Index(),
      {parent, parent_values}, {child, child_values}, site, Creation(parent, site), cells, {0}, {1}));
  test::Check(binding.binding.captures.size() == count);
  const FunctionBinding functions[]{binding.binding};
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(parent_values.graph, {},
      IncomingCells::kConditional, functions));
  // Parent and child have identically numbered local cells before the child
  // incoming range. Their generated names must not shadow the actual aliases.
  TreeBinding(parent_values, child_values, evaluation, cells);
  for (std::uint32_t i = 0; i < count; ++i) {
    test::Check(cells.targets[i].child == CellId{count + i});
    test::Check(std::get<ParentCell>(cells.targets[i].target).cell == CellId{i});
    test::Check(binding.binding.captures[i] == CellId{i});
  }
}
void CellDefaultDiscovery() {
  constexpr std::uint8_t kSave = 0x61, kUndefined = 0x00, kNull = 0x01,
      kStrictEqual = 0x28, kJumpIfFalse = 0x4f, kJoinDistance = 5;
  const auto body = test::Body({kPush, 1, kLoad, 1, kStoreCell, 0,
      kReadCell, 0, kSave, 0, kUndefined, kStrictEqual, 0, 0,
      kJumpIfFalse, kJoinDistance, kNull, kStoreCell, 0, kReadCell, 0, kReturn});
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {false, false, false, false}));
  const auto input = std::get<FunctionInput>(FunctionInput::Build(Identity(), kParent, kRecord,
      body, frame, InputCaptureMode::kConditional));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(values.graph, 1));
  const auto found = std::get<CellDefaults>(FindCellDefaults(values.graph, *values.cells, parameters, 0));
  test::Check(found.candidates.size() == 1);
  const auto& candidate = found.candidates[0];
  test::Check(candidate.parameter == 0 && candidate.entry == parameters.formals[0].entry &&
      std::get<SpecialValue>(candidate.fallback) == SpecialValue::kNull);
  test::Check(values.cells->operations[candidate.initial_store.value] == candidate.cell &&
      values.cells->operations[candidate.fallback_store.value] == candidate.cell);
  const auto prove = [&](const CellDefaults& proposed, SemanticBudget budget = {}) {
    return CellDefaultProof::Build(values.graph, *values.environments, *values.cells,
        parameters, proposed, 0, budget);
  };
  const auto proved = std::get<CellDefaultProof>(prove(found));
  test::Check(proved.Defaults().size() == 1 && proved.Revision() == values.graph.revision &&
      proved.ControlRevision() == values.graph.cfg_revision && proved.BodyEntry() == candidate.join);
  test::Check(std::none_of(proved.Operations().begin(), proved.Operations().end(),
      [&](const auto& moved) { return moved.operation == candidate.initial_store; }));
  auto forged = found; forged.candidates[0].fallback = analysis::IntegerLiteral{7};
  test::Check(std::get<CellDefaultProofError>(prove(forged)) == CellDefaultProofError::kLiteral);
  forged = found; std::swap(forged.candidates[0].missing, forged.candidates[0].present);
  test::Check(std::get<CellDefaultProofError>(prove(forged)) == CellDefaultProofError::kControl);
  forged = found; ++forged.revision.value;
  test::Check(std::get<CellDefaultProofError>(prove(forged)) == CellDefaultProofError::kRevision);
  SemanticBudget proof_budget; proof_budget.work = proved.Work();
  test::Check(std::holds_alternative<CellDefaultProof>(prove(found, proof_budget)));
  --proof_budget.work;
  test::Check(std::get<CellDefaultProofError>(prove(found, proof_budget)) == CellDefaultProofError::kWorkLimit);
  const auto residual = std::get<DefaultBody>(DefaultBody::BuildCell(values.graph,
      *values.environments, *values.cells, parameters, found, 0));
  const auto& graph = residual.Graph();
  test::Check(graph.revision.value == values.graph.revision.value + 1 &&
      graph.cfg_revision.value == values.graph.cfg_revision.value + 1);
  test::Check(graph.ops[candidate.initial_store.value].life == ValueLife::kLive &&
      std::holds_alternative<ValueLexicalStore>(graph.ops[candidate.initial_store.value].operation));
  for (const auto moved : residual.Signature().Operations()) {
    test::Check(graph.ops[moved.operation.value].life == ValueLife::kRemoved &&
        std::holds_alternative<ValueNop>(graph.ops[moved.operation.value].operation));
    const auto origin = graph.ops[moved.operation.value].origin;
    test::Check(graph.origins[origin.value].pc == values.graph.origins[origin.value].pc);
  }
  const auto rebuilt = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
  const auto rebound = std::get<CellPlan>(BuildCellPlan(graph, rebuilt));
  test::Check(rebound.cells.size() == values.cells->cells.size());
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (const auto callable : {CallableKind::kMethod, CallableKind::kFunction, CallableKind::kArrow}) {
    FunctionSyntax syntax;
    syntax.identity = Identity(); syntax.method = kParent; syntax.owner = {0};
    syntax.callable = callable; syntax.strictness = Strictness::kStrict; syntax.parameters.resize(1);
    auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation,
        std::move(syntax), {constants, 0, 0})).tree;
    test::Check(std::holds_alternative<DefaultSignatureProof>(BuildDefaultSignature(residual, tree)));
    test::Check(tree.parameters[0].initializer && tree.signature.size() == residual.Signature().Operations().size());
    test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, {constants, 0, 0})));
    const auto initializer = *tree.parameters[0].initializer;
    tree.expressions[initializer.value].expression = TreeLiteral{UndefinedLiteral{}};
    test::Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(residual, tree)) == DefaultSignatureError::kLiteral);
  }
  test::Check(std::get<CellDefaults>(FindCellDefaults(values.graph, *values.cells, parameters, 1)).candidates.empty());
  auto stale = parameters; ++stale.revision.value;
  test::Check(std::get<CellDefaultError>(FindCellDefaults(values.graph, *values.cells, stale, 0)) == CellDefaultError::kRevision);
  SemanticBudget budget; budget.bytes = 0;
  test::Check(std::get<CellDefaultError>(FindCellDefaults(values.graph, *values.cells, parameters, 0, budget)) == CellDefaultError::kResourceLimit);
  budget = {}; budget.work = found.work - 1;
  test::Check(std::get<CellDefaultError>(FindCellDefaults(values.graph, *values.cells, parameters, 0, budget)) == CellDefaultError::kWorkLimit);
  auto changed = values.graph;
  std::get<ValueBinary>(changed.ops[candidate.comparison.value].operation).op = BinaryOperator::kStrictNotEqual;
  test::Check(std::get<CellDefaults>(FindCellDefaults(changed, *values.cells, parameters, 0)).candidates.empty());
}
void CreationEvaluation(bool pop) {
  std::vector<std::uint8_t> code{kPush, 1, kFunction, 0, 0, 0, 0};
  if (pop) code.push_back(kPop);
  code.push_back(kReturn);
  const auto body = test::Body(code);
  const auto child_body = test::Body({kReturnUndefined});
  const auto parent = Input(kParent, body);
  const auto child = Input(kChild, child_body);
  const auto parent_values = std::get<FunctionValues>(BuildFunctionValues(parent, body, Frame()));
  const auto child_values = std::get<FunctionValues>(BuildFunctionValues(child, child_body, Frame()));
  const auto site = parent.Children()[0];
  const auto creation = Creation(parent, site);
  const auto cells = std::get<CaptureCells>(BindCaptureCells(Index(), {parent, parent_values},
      {child, child_values}, site, creation));
  const auto binding = std::get<CapturedFunctionBinding>(BindCaptureEvaluation(Index(),
      {parent, parent_values}, {child, child_values}, site, creation, cells, {0}, {1}));
  const FunctionBinding functions[]{binding.binding};
  auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(parent_values.graph, {},
      IncomingCells::kConditional, functions));
  const auto operation = binding.binding.creation;
  test::Check(evaluation.operations[operation.value].kind ==
      (pop ? EvaluationKind::kRoot : EvaluationKind::kInline));
  if (pop) {
    // A forged otherwise-once-only return expression must not move creation
    // out of the implicit lexical environment in which ABC executed it.
    evaluation.operations[operation.value] = {EvaluationKind::kInline,
        parent_values.graph.ops.back().first_use, {}};
    test::Check(std::get<EvaluationError>(VerifyEvaluationPlan(parent_values.graph, evaluation)) == EvaluationError::kOrder);
  }
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Occurrences(count);
  for (const auto count : {1U, 2U, 200U, 201U}) DistinctCells(static_cast<std::uint8_t>(count));
  ForwardingAndChildren();
  ClosedAdmission();
  CreationEvaluation(false); CreationEvaluation(true);
  CellDefaultDiscovery();
}
