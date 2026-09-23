#include "abcd/source/created_function.h"

#include "abcd/abc/call_type.h"
#include "function_template.h"
#include "runtime_name.h"

namespace abcd::source {
namespace {
CreatedFunctionResult WriteCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget, abc::StringBudget strings,
    analysis::CapturedValues captured_values, const abc::ModuleInfo* module) {
  const auto lifted = analysis::LiftFunctionCreation(instruction);
  if (const auto* error = std::get_if<analysis::CreationError>(&lifted))
    return *error;
  const auto creation = std::get<analysis::FunctionCreation>(lifted);
  const auto resolved = abc::ResolveMethodTarget(file, header, index, lookup,
                                                 caller, creation.method);
  if (const auto* error = std::get_if<abc::MethodTargetError>(&resolved))
    return *error;
  if (const auto* error = std::get_if<abc::MetadataError>(&resolved))
    return *error;
  if (const auto* error = std::get_if<abc::MethodAttributeError>(&resolved))
    return *error;
  const auto target = std::get<abc::MethodTarget>(resolved);
  const auto& method = index.methods[target.position.value];
  if (module) {
    const auto caller_position = lookup.Find(caller);
    if (!caller_position) return abc::MethodTargetError::kCallerMissing;
    if (method.owner_offset != index.methods[caller_position->value].owner_offset)
      return CreatedFunctionError::kModuleOwner;
  }
  if (!method.code) return CreatedFunctionError::kMissingCode;

  const auto name = RuntimeName(method);
  if (!name) return CreatedFunctionError::kName;
  const auto call = abc::ReadCallType(file, header, index, method.annotations,
                                      {budget.bytes});
  if (const auto* error = std::get_if<abc::MetadataError>(&call)) return *error;
  const auto frame = abc::BuildFrameLayout(
      method.code->registers, method.code->arguments,
      abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
  if (const auto* error = std::get_if<abc::FrameError>(&frame)) return *error;
  const auto decoded =
      abc::DecodeMethodBody(file, header.profile, *method.code, {budget.bytes});
  if (const auto* error = std::get_if<abc::BodyFailure>(&decoded))
    return *error;
  const auto& body = std::get<abc::MethodBody>(decoded);
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > budget.bytes / width) return false;
    budget.bytes -= count * width;
    return true;
  };
  // Decoder transient buffers have died; all retained vector capacities remain
  // live alongside source scratch and must be admitted, not only their sizes.
  if (!admit(body.instructions.capacity(), sizeof(abc::BodyInstruction)) ||
      !admit(body.tries.capacity(), sizeof(abc::TryRegion)) ||
      !admit(body.handlers.capacity(), sizeof(abc::CatchHandler)))
    return CreatedFunctionError::kResourceLimit;
  const MethodConstants constants{file, header, index, method.offset, strings,
                                  module};
  const BaseFunction function{creation, target.attributes,
                              std::get<abc::FrameLayout>(frame), *name};
  const auto emitted =
      captured_values == analysis::CapturedValues::kLanguage
          ? WriteBaseFunctionTemplate(function, body, output, budget,
                                      &constants)
          : WriteBaseFunction(function, body, output, budget, &constants);
  return std::visit([](auto value) -> CreatedFunctionResult { return value; },
                    emitted);
}
}  // namespace
CreatedFunctionResult WriteCreatedFunction(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings, const abc::ModuleInfo* module) {
  return WriteCreation(file, header, index, lookup, caller, instruction, output,
                       budget, strings, analysis::CapturedValues::kReject,
                       module);
}
CreatedFunctionResult WriteCreatedFunctionTemplate(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings, const abc::ModuleInfo* module) {
  return WriteCreation(file, header, index, lookup, caller, instruction, output,
                       budget, strings, analysis::CapturedValues::kLanguage,
                       module);
}
}  // namespace abcd::source
