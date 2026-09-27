#include "phased_capture_input.h"

#include <bit>

namespace abcd::source {
namespace {
struct CheckedSite { analysis::ExceptionNodeId before; std::uint64_t work; };
std::variant<CheckedSite, CaptureError, InputBindingError> CheckSite(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent,
    const PhasedCreation& creation,
    const FunctionInput& child, const FunctionContext& owner, analysis::SemanticBudget budget) {
  const std::uint64_t owner_work = 1 + std::bit_width(index.methods.size());
  if (budget.work < owner_work) return CaptureError::kWorkLimit;
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
  return CheckedSite{phase.input, owner_work};
}
}  // namespace
InputBindingResult BindPhasedFunctionInputs(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreation& creation,
    const FunctionInput& child, const FunctionContext& owner, analysis::SemanticBudget budget) {
  const auto checked = CheckSite(index, lookup, parent, creation, child, owner, budget);
  if (const auto* error = std::get_if<CaptureError>(&checked)) return *error;
  if (const auto* error = std::get_if<InputBindingError>(&checked)) return *error;
  const auto site = std::get<CheckedSite>(checked); budget.work -= site.work;
  const auto input = parent.TreeInput(body);
  const ScopeFacts scopes(ExceptionScopeFacts{input.input.flow, input.input.environments, locals, site.before});
  auto built = BuildCapturePlan(scopes, creation.instruction, creation.binding, child.Captures(), budget);
  if (const auto* error = std::get_if<CaptureError>(&built)) return *error;
  auto result = std::get<CapturePlan>(std::move(built));
  result.work += site.work;
  return result;
}
std::variant<std::uint64_t, CaptureError, InputBindingError> VerifyPhasedFunctionInputs(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreation& creation,
    const FunctionInput& child, const FunctionContext& owner, const CapturePlan& plan,
    analysis::SemanticBudget budget) {
  const auto checked = CheckSite(index, lookup, parent, creation, child, owner, budget);
  if (const auto* error = std::get_if<CaptureError>(&checked)) return *error;
  if (const auto* error = std::get_if<InputBindingError>(&checked)) return *error;
  const auto site = std::get<CheckedSite>(checked); budget.work -= site.work;
  const auto input = parent.TreeInput(body);
  const ScopeFacts scopes(ExceptionScopeFacts{input.input.flow, input.input.environments, locals, site.before});
  const auto verified = VerifyCapturePlan(scopes, creation.instruction, creation.binding, child.Captures(), plan, budget);
  if (const auto* error = std::get_if<CaptureError>(&verified)) return *error;
  return site.work + std::get<std::uint64_t>(verified);
}
}  // namespace abcd::source
