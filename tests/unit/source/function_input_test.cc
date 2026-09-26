#include "../../../src/source/function_input.h"
#include "../../../src/source/capture_cells.h"
#include "abcd/source/value_stage.h"
#include "analysis/ssa_fixture.h"

#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
static_assert(!std::is_copy_constructible_v<FunctionInput>);
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
  for (std::uint32_t i = 0; i < count; ++i) {
    test::Check(cells.targets[i].child == CellId{count + i});
    test::Check(std::get<ParentCell>(cells.targets[i].target).cell == CellId{i});
  }
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Occurrences(count);
  for (const auto count : {1U, 2U, 200U, 201U}) DistinctCells(static_cast<std::uint8_t>(count));
  ForwardingAndChildren();
  ClosedAdmission();
}
