#include "phased_capture_cells.h"

#include "capture_cell_targets.h"

namespace abcd::source {
namespace {
CaptureCellError InputError(CaptureError error) {
  if (error == CaptureError::kResourceLimit) return CaptureCellError::kResourceLimit;
  if (error == CaptureError::kWorkLimit) return CaptureCellError::kWorkLimit;
  return CaptureCellError::kInput;
}
CaptureParent Parent(const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreationSite& creation, const CellPlan* cells) {
  const auto input = parent.TreeInput(body).input;
  const auto& graph = parent.Graph();
  const auto before = graph.phases[graph.ops[creation.operation.value].block.value].input;
  return {parent.Identity(), parent.Target(), graph, parent.CellEnvironments(), cells ? cells : parent.Cells(),
      input.flow.instructions, locals.plan.allocations,
      ScopeFacts(ExceptionScopeFacts{input.flow, input.environments, locals, before})};
}
}  // namespace
std::variant<CaptureCells, CaptureCellError> BindPhasedCaptureCells(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreationSite& creation,
    CaptureChild child, const FunctionContext& owner, analysis::SemanticBudget budget, const CellPlan* parent_cells) {
  auto bound = BindPhasedFunctionInputs(index, lookup, parent, body, locals, creation, child.Input(), owner, budget);
  if (const auto* error = std::get_if<CaptureError>(&bound)) return InputError(*error);
  if (std::holds_alternative<InputBindingError>(bound)) return CaptureCellError::kOwner;
  auto input = std::get<CapturePlan>(std::move(bound));
  const auto work = input.work; budget.work -= work;
  auto built = BindCaptureTargets(Parent(parent, body, locals, creation, parent_cells), child,
      creation.instruction, creation.binding, std::move(input), budget);
  if (const auto* error = std::get_if<CaptureCellError>(&built)) return *error;
  auto result = std::get<CaptureCells>(std::move(built)); result.work += work;
  return result;
}
std::variant<std::uint64_t, CaptureCellError> VerifyPhasedCaptureCells(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreationSite& creation,
    CaptureChild child, const FunctionContext& owner, const CaptureCells& plan,
    analysis::SemanticBudget budget, const CellPlan* parent_cells) {
  const auto checked = VerifyPhasedFunctionInputs(index, lookup, parent, body, locals, creation,
      child.Input(), owner, plan.input, budget);
  if (const auto* error = std::get_if<CaptureError>(&checked)) return InputError(*error);
  if (std::holds_alternative<InputBindingError>(checked)) return CaptureCellError::kOwner;
  const auto work = std::get<std::uint64_t>(checked); budget.work -= work;
  const auto verified = VerifyCaptureTargets(Parent(parent, body, locals, creation, parent_cells), child,
      creation.instruction, creation.binding, plan, budget);
  if (const auto* error = std::get_if<CaptureCellError>(&verified)) return *error;
  return work + std::get<std::uint64_t>(verified);
}
}  // namespace abcd::source
