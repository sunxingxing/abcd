#include "../../../src/source/function_input.h"
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
  const auto parent = Input(kParent, test::Body({kFunction, 0, 0, 0, 1, kReturnUndefined}));
  const auto child = Input(kChild, test::Body({kReadCell, 0, kStoreCell, 0, kFunction, 0, 0, 0, 1, kReturnUndefined}));
  const auto site = parent.Children()[0];
  const auto bound = std::get<CapturePlan>(BindFunctionInputs(Index(), parent, site, Creation(parent, site), child));
  test::Check(bound.bindings.size() == 2 && std::holds_alternative<CapturedSlot>(bound.bindings[0].cell));
  test::Check(child.Children().size() == 1 && child.Proof().closure_writes == ClosureWrites::kLanguage);
  // Binding direct demands does not mutate/discharge the child's descendants.
  test::Check(child.Children()[0].value == 2 && child.Captures().size() == 2);
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
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Occurrences(count);
  ForwardingAndChildren();
  ClosedAdmission();
}
