#include "abcd/analysis/creation_binding.h"

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

CreationBindingResult BindCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::MethodBody& body,
    const EnvironmentFlow& flow, std::size_t instruction) {
  if (instruction >= body.instructions.size())
    return CreationBindingError::kInstructionRange;
  if (flow.before.size() != body.instructions.size())
    return CreationBindingError::kInvalidFlow;
  const auto& site = body.instructions[instruction];
  const auto lifted = LiftFunctionCreation(site.decoded);
  if (const auto* error = std::get_if<CreationError>(&lifted)) return *error;
  if (!flow.before[instruction]) return CreationBindingError::kUnreachable;
  const auto environment = *flow.before[instruction];
  if (const auto* local = std::get_if<EnvironmentId>(&environment))
    if (!flow.environments.Find(*local))
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
}  // namespace abcd::analysis
