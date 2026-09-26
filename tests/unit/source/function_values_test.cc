#include "../../../src/source/function_values.h"
#include "../../../src/source/function_captures.h"
#include "../../../src/source/cell_evaluation.h"
#include "abcd/source/value_stage.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
// Dynamic 12.0.6.0 opcodes from the pinned v7 ISA, not source-text fixtures.
constexpr std::uint8_t kLoad = 0x60, kStoreCell = 0x3d, kReadCell = 0x3c,
    kReturn = 0x64, kReturnUndefined = 0x65;
constexpr abc::EntityOffset kMethod{300}, kRecord{100};
abc::FrameLayout Frame() {
  return std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
}
RouteInputIdentity Identity() {
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  identity.abc_version = {12, 0, 6, 0};
  return identity;
}
FunctionInput Input(const abc::MethodBody& body, InputCaptureMode mode) {
  return std::get<FunctionInput>(FunctionInput::Build(Identity(), kMethod, kRecord, body, Frame(), mode));
}
void Captures(std::uint32_t count) {
  std::vector<std::uint8_t> bytes;
  for (std::uint32_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {kReadCell, 0, kStoreCell, 0});
  bytes.push_back(kReturnUndefined);
  const auto body = test::Body(bytes);
  const auto input = Input(body, InputCaptureMode::kConditional);
  auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, Frame()));
  test::Check(values.capture_mode == InputCaptureMode::kConditional);
  test::Check(values.environments.has_value() == (count != 0));
  if (count == 0) return;
  const auto checked = VerifyFunctionCaptures(input, values.graph, *values.environments);
  test::Check(std::holds_alternative<std::uint64_t>(checked));
  SemanticBudget exact; exact.work = std::get<std::uint64_t>(checked);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyFunctionCaptures(input, values.graph, *values.environments, exact)));
  --exact.work;
  test::Check(std::get<FunctionCaptureError>(VerifyFunctionCaptures(input, values.graph,
      *values.environments, exact)) == FunctionCaptureError::kWorkLimit);
  const auto cells = std::get<CellPlan>(BuildCellPlan(values.graph, *values.environments));
  test::Check(cells.cells.size() == 1 && cells.accesses.size() == 2 * count &&
      std::holds_alternative<IncomingCell>(cells.cells[0].storage));
  // Working correspondence does not authorize a child AST or local declaration.
  test::Check(std::holds_alternative<CellEvaluationError>(BuildCellEvaluation(values.graph)));
  for (std::size_t i = 0; i < input.CaptureAccesses().size(); ++i) {
    const auto site = input.CaptureAccesses()[i].instruction;
    const auto op = *values.graph.input.sites[site.value];
    test::Check(cells.operations[op.value] == CellId{0});
  }
  const auto site = input.CaptureAccesses().front().instruction.value;
  const auto saved = values.graph.input.sites[site];
  values.graph.input.sites[site].reset();
  test::Check(std::holds_alternative<FunctionCaptureError>(VerifyFunctionCaptures(input, values.graph, *values.environments)));
  values.graph.input.sites[site] = saved;
  auto& origin = values.graph.origins[values.graph.ops[saved->value].origin.value];
  const auto pc = origin.pc; ++origin.pc.value;
  test::Check(std::holds_alternative<FunctionCaptureError>(VerifyFunctionCaptures(input, values.graph, *values.environments)));
  origin.pc = pc;
  auto& binding = values.environments->flow.bindings.front();
  ++binding.binding.slot.value;
  test::Check(std::holds_alternative<FunctionCaptureError>(VerifyFunctionCaptures(input, values.graph, *values.environments)));
  --binding.binding.slot.value;
  ++values.environments->revision.value;
  test::Check(std::get<FunctionCaptureError>(VerifyFunctionCaptures(input, values.graph,
      *values.environments)) == FunctionCaptureError::kEnvironment);
}
void ClosedRoute() {
  const auto body = test::Body({kLoad, 0, kReturn});
  const auto input = Input(body, InputCaptureMode::kClosed);
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, Frame()));
  const auto route = BuildValueStage(Identity(), kMethod, kRecord, body, Frame());
  test::Check(values.capture_mode == InputCaptureMode::kClosed && !values.environments);
  test::Check(route.graph && route.receipt.stage == SourceStage::kValuesVerified && !route.receipt.failure);
  test::Check(route.graph->ops.size() == values.graph.ops.size() &&
      route.graph->revision == values.graph.revision);
  const auto captured = test::Body({kReadCell, 0, kReturn});
  const auto conditional = Input(captured, InputCaptureMode::kConditional);
  test::Check(std::holds_alternative<FunctionValues>(BuildFunctionValues(conditional, captured, Frame())));
  const auto refused = BuildValueStage(Identity(), kMethod, kRecord, captured, Frame());
  test::Check(!refused.graph && refused.receipt.failure->reason == ValueRouteError::kInputStage);
  SemanticBudget empty; empty.bytes = 0;
  const auto failure = std::get<FunctionValueFailure>(BuildFunctionValues(input, body, Frame(), empty));
  test::Check(failure.stage == SourceStage::kInputVerified && !failure.revision &&
      failure.cause.failure_class == RouteFailureClass::kMemoryLimit &&
      failure.cause.reason == ValueRouteError::kImportStage);
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Captures(count);
  ClosedRoute();
}
