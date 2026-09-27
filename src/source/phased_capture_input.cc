#include "phased_capture_input.h"

#include <bit>

namespace abcd::source {
InputBindingResult BindPhasedFunctionInputs(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreation& creation,
    const FunctionInput& child, const FunctionContext& owner, analysis::SemanticBudget budget) {
  const auto owner_work = 1 + std::bit_width(index.methods.size());
  if (budget.work < owner_work) return CaptureError::kWorkLimit;
  budget.work -= owner_work;
  if (parent.Identity() != child.Identity() || !child.MatchesOwner(owner)) return InputBindingError::kIdentity;
  const auto parent_position = lookup.Find(parent.Target());
  if (!parent_position || parent_position->value >= index.methods.size() ||
      index.methods[parent_position->value].offset != parent.Target() ||
      index.methods[parent_position->value].owner_offset != child.Record()) return InputBindingError::kIdentity;
  const auto* occurrence = owner.CreationSite();
  if (!occurrence || occurrence->creator != parent.Target() || occurrence->target != child.Method() ||
      occurrence->pc != creation.binding.offset ||
      occurrence->resolved.position != creation.binding.target.position) return InputBindingError::kTarget;
  if (child.Mode() != InputCaptureMode::kConditional) return InputBindingError::kMode;
  const auto target = creation.binding.target.position.value;
  if (target >= index.methods.size() || index.methods[target].offset != child.Method() ||
      index.methods[target].owner_offset != child.Record()) return InputBindingError::kTarget;
  const auto& graph = parent.Graph();
  if (creation.operation.value >= graph.ops.size() || creation.instruction.value >= graph.input.sites.size() ||
      graph.input.sites[creation.instruction.value] != creation.operation) return CaptureError::kCreation;
  const auto& op = graph.ops[creation.operation.value];
  if (op.life != analysis::ValueLife::kLive || op.block.value >= graph.phases.size() ||
      !std::holds_alternative<analysis::ValueFunctionCreation>(op.operation)) return CaptureError::kCreation;
  const auto& phase = graph.phases[op.block.value];
  if (phase.phase != analysis::ExceptionPhase::kBefore || phase.operation != creation.operation ||
      phase.instruction.value != creation.instruction.value) return CaptureError::kCreation;
  const auto input = parent.TreeInput(body);
  const ScopeFacts scopes(ExceptionScopeFacts{input.input.flow, input.input.environments, locals, phase.input});
  auto built = BuildCapturePlan(scopes, creation.instruction, creation.binding, child.Captures(), budget);
  if (const auto* error = std::get_if<CaptureError>(&built)) return *error;
  auto result = std::get<CapturePlan>(std::move(built));
  result.work += owner_work;
  return result;
}
}  // namespace abcd::source
