#include "abcd/analysis/creation_binding.h"
#include "abcd/analysis/exception_flow.h"
#include "abcd/analysis/object_creation.h"

namespace abcd::analysis {
std::variant<LexicalBinding, EnvironmentError, CaptureRebaseError>
RebaseCapture(const EnvironmentIndex& caller, const CreationCapture& creation,
              CapturedSlot captured) {
  const auto* ordinary = std::get_if<OrdinaryCapture>(&creation);
  if (!ordinary) return CaptureRebaseError::kSendableEnvironment;
  const auto resolved = caller.ResolveCapture(ordinary->environment,
                                              captured.depth, captured.slot);
  if (const auto* error = std::get_if<EnvironmentError>(&resolved))
    return *error;
  return std::get<LexicalBinding>(resolved);
}

namespace {
CreationBindingResult BindAtEnvironment(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::MethodBody& body,
    const EnvironmentIndex& environments, const std::optional<EnvironmentRef>& captured,
    std::size_t instruction) {
  if (instruction >= body.instructions.size())
    return CreationBindingError::kInstructionRange;
  const auto& site = body.instructions[instruction];
  const auto lifted = LiftFunctionCreation(site.decoded);
  if (const auto* error = std::get_if<CreationError>(&lifted)) return *error;
  if (!captured) return CreationBindingError::kUnreachable;
  const auto environment = *captured;
  if (const auto* local = std::get_if<EnvironmentId>(&environment))
    if (!environments.Find(*local))
      return CreationBindingError::kInvalidEnvironment;
  const auto creation = std::get<FunctionCreation>(lifted);
  const auto resolved = abc::ResolveMethodTarget(file, header, index, lookup,
                                                 caller, creation.method);
  if (const auto* error = std::get_if<abc::MethodTargetError>(&resolved))
    return *error;
  if (const auto* error = std::get_if<abc::MetadataError>(&resolved))
    return *error;
  if (const auto* error = std::get_if<abc::MethodAttributeError>(&resolved))
    return *error;
  const auto target = std::get<abc::MethodTarget>(resolved);
  // RuntimeDefinefunc branches on sendable; RuntimeDefineMethod always sets
  // the supplied ordinary env. Do not dispatch capture mode on attributes
  // alone.
  CreationCapture capture = OrdinaryCapture{environment};
  if (creation.kind == CreationKind::kFunction && target.attributes.sendable)
    capture = SendableCapture{};
  return CreationBinding{site.offset, creation, target, capture};
}
CreationBindingResult BindLiteralAtEnvironment(
    const abc::MethodBody& body, const EnvironmentIndex& environments,
    const std::optional<EnvironmentRef>& captured, std::size_t instruction,
    const FunctionCreationSite& site) {
  if (instruction >= body.instructions.size())
    return CreationBindingError::kInstructionRange;
  const auto& at = body.instructions[instruction];
  const auto lifted = LiftObjectCreation(at.decoded);
  if (!std::holds_alternative<BufferedObjectCreation>(lifted) || at.offset != site.pc ||
      site.property == kNoLiteralProperty ||
      site.creation.home_object != HomeObjectInput::kLiteralObject)
    return CreationError::kUnsupportedOpcode;
  if (!captured) return CreationBindingError::kUnreachable;
  if (const auto* local = std::get_if<EnvironmentId>(&*captured))
    if (!environments.Find(*local))
      return CreationBindingError::kInvalidEnvironment;
  return CreationBinding{at.offset, site.creation, site.resolved,
                         OrdinaryCapture{*captured}, site.property};
}
}  // namespace
CreationBindingResult BindLiteralCreation(
    const abc::MethodBody& body, const EnvironmentFlow& flow, std::size_t instruction,
    const FunctionCreationSite& site) {
  if (instruction >= body.instructions.size()) return CreationBindingError::kInstructionRange;
  if (flow.before.size() != body.instructions.size()) return CreationBindingError::kInvalidFlow;
  return BindLiteralAtEnvironment(body, flow.environments, flow.before[instruction],
                                  instruction, site);
}
CreationBindingResult BindExceptionLiteralCreation(
    const abc::MethodBody& body, const ExceptionFlow& flow,
    const EnvironmentFlow& environments, ExceptionNodeId before,
    const FunctionCreationSite& site) {
  if (before.value >= flow.nodes.size()) return CreationBindingError::kInstructionRange;
  if (environments.before.size() != flow.nodes.size() || flow.instructions.size() != body.instructions.size())
    return CreationBindingError::kInvalidFlow;
  const auto& node = flow.nodes[before.value];
  if (node.phase != ExceptionPhase::kBefore || node.instruction.value >= body.instructions.size() ||
      flow.instructions[node.instruction.value].pc != body.instructions[node.instruction.value].offset)
    return CreationBindingError::kInvalidFlow;
  return BindLiteralAtEnvironment(body, environments.environments,
      environments.before[before.value], node.instruction.value, site);
}
CreationBindingResult BindCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::MethodBody& body,
    const EnvironmentFlow& flow, std::size_t instruction) {
  if (instruction >= body.instructions.size()) return CreationBindingError::kInstructionRange;
  if (flow.before.size() != body.instructions.size()) return CreationBindingError::kInvalidFlow;
  return BindAtEnvironment(file, header, index, lookup, caller, body, flow.environments,
      flow.before[instruction], instruction);
}
CreationBindingResult BindExceptionCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::MethodBody& body, const ExceptionFlow& flow,
    const EnvironmentFlow& environments, ExceptionNodeId before) {
  if (before.value >= flow.nodes.size()) return CreationBindingError::kInstructionRange;
  if (environments.before.size() != flow.nodes.size() || flow.instructions.size() != body.instructions.size())
    return CreationBindingError::kInvalidFlow;
  const auto& node = flow.nodes[before.value];
  if (node.phase != ExceptionPhase::kBefore || node.instruction.value >= body.instructions.size() ||
      flow.instructions[node.instruction.value].pc != body.instructions[node.instruction.value].offset)
    return CreationBindingError::kInvalidFlow;
  return BindAtEnvironment(file, header, index, lookup, caller, body, environments.environments,
      environments.before[before.value], node.instruction.value);
}
}  // namespace abcd::analysis
