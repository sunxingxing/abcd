#include "abcd/source/created_function.h"

#include <optional>
#include <utility>
#include <vector>

#include "abcd/abc/call_type.h"
#include "abcd/analysis/method_roles.h"
#include "function_template.h"
#include "parameter_names.h"
#include "runtime_name.h"
#include "creation_expansion.h"

namespace abcd::source {
namespace {
CreatedFunctionResult WriteCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const analysis::FunctionCreation& creation,
    std::ostream& output, LocalLexicalBudget budget, abc::StringBudget strings,
    analysis::CapturedValues captured_values, const abc::ModuleInfo* module,
    const analysis::MethodRoleIndex* roles,
    const GlobalScope* global_scope, CreationExpansion* expansion = nullptr) {
  const auto resolved = abc::ResolveMethodTarget(file, header, index, lookup,
                                                 caller, creation.method);
  if (const auto* error = std::get_if<abc::MethodTargetError>(&resolved))
    return *error;
  if (const auto* error = std::get_if<abc::MetadataError>(&resolved))
    return *error;
  if (const auto* error = std::get_if<abc::MethodAttributeError>(&resolved))
    return *error;
  const auto target = std::get<abc::MethodTarget>(resolved);
  std::optional<ExpansionFrame> admission;
  if (expansion) {
    admission.emplace(*expansion, index.methods[target.position.value].offset,
                      budget);
    if (const auto error = admission->error())
      return *error == ExpansionError::kCycle
                 ? CreatedFunctionError::kCreationCycle
                 : CreatedFunctionError::kResourceLimit;
  }
  // Shared MethodLiteral kinds can change during class materialization. An
  // arrow is source-safe only when a complete same-file role index proves no
  // constructor or method-bearing literal can refer to this target.
  if ((target.attributes.kind == abc::FunctionKind::kArrow ||
       target.attributes.kind == abc::FunctionKind::kAsyncArrow) &&
      (!roles || roles->At(target.position) != analysis::MethodRole::kUnseen))
    return CreatedFunctionError::kRole;
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
  std::optional<RecoveredParameters> recovered;
  std::vector<std::string_view> parameters;
  auto source_names = RecoverParameterNames(
      file, header, method, std::get<abc::FrameLayout>(frame), body,
      budget.bytes, strings, *name);
  if (const auto* found = std::get_if<RecoveredParameters>(&source_names)) {
    recovered.emplace(std::move(*found));
    if (recovered->AllocatedBytes() > budget.bytes ||
        recovered->names.size() >
            (budget.bytes - recovered->AllocatedBytes()) /
                sizeof(std::string_view))
      return CreatedFunctionError::kResourceLimit;
    budget.bytes -= recovered->AllocatedBytes() +
                    recovered->names.size() * sizeof(std::string_view);
    parameters.reserve(recovered->names.size());
    if (parameters.capacity() > recovered->names.size()) {
      const auto extra = parameters.capacity() - recovered->names.size();
      if (extra > budget.bytes / sizeof(std::string_view))
        return CreatedFunctionError::kResourceLimit;
      budget.bytes -= extra * sizeof(std::string_view);
    }
    for (const auto& name : recovered->names) parameters.push_back(name);
  } else if (const auto* error =
                 std::get_if<abc::MetadataError>(&source_names)) {
    return *error;
  } else if (std::get<ParameterNameError>(source_names) ==
             ParameterNameError::kResourceLimit) {
    return CreatedFunctionError::kResourceLimit;
  }
  MethodConstants constants{file, header, index, method.offset, strings,
                                  module, roles,
                                  global_scope
                                      ? std::optional<GlobalScope>(*global_scope)
                                      : std::nullopt,
                                  parameters};
  if (expansion) {
    constants.leaf_methods = &lookup;
    constants.expansion = expansion;
  }
  const BaseFunction function{creation, target.attributes,
                              std::get<abc::FrameLayout>(frame), *name};
  FunctionEmitResult emitted;
  if (target.attributes.kind == abc::FunctionKind::kArrow ||
      target.attributes.kind == abc::FunctionKind::kAsyncArrow) {
    emitted = WriteArrowFunction(function, body, output, budget, &constants,
                                 captured_values);
  } else {
    emitted = captured_values == analysis::CapturedValues::kLanguage
                  ? WriteBaseFunctionTemplate(function, body, output, budget,
                                              &constants)
                  : WriteBaseFunction(function, body, output, budget,
                                      &constants);
  }
  return std::visit([](auto value) -> CreatedFunctionResult { return value; },
                    emitted);
}
}  // namespace
CreatedFunctionResult WriteCreatedFunction(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings, const abc::ModuleInfo* module,
    const analysis::MethodRoleIndex* roles,
    const GlobalScope* global_scope) {
  const auto lifted = analysis::LiftFunctionCreation(instruction);
  if (const auto* error = std::get_if<analysis::CreationError>(&lifted))
    return *error;
  return WriteCreatedFunction(file, header, index, lookup, caller,
                             std::get<analysis::FunctionCreation>(lifted),
                             output, budget, strings, module, roles, global_scope);
}
CreatedFunctionResult WriteCreatedFunction(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const analysis::FunctionCreation& creation,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings, const abc::ModuleInfo* module,
    const analysis::MethodRoleIndex* roles,
    const GlobalScope* global_scope) {
  CreationExpansion expansion(caller, budget.bytes, budget.work);
  return WriteCreation(file, header, index, lookup, caller, creation, output,
                       budget, strings, analysis::CapturedValues::kReject,
                       module, roles, global_scope, &expansion);
}
CreatedFunctionResult WriteCreatedFunctionTemplate(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings, const abc::ModuleInfo* module,
    const analysis::MethodRoleIndex* roles,
    const GlobalScope* global_scope) {
  const auto lifted = analysis::LiftFunctionCreation(instruction);
  if (const auto* error = std::get_if<analysis::CreationError>(&lifted))
    return *error;
  return WriteCreatedFunctionTemplate(file, header, index, lookup, caller,
                       std::get<analysis::FunctionCreation>(lifted), output,
                       budget, strings, module, roles, global_scope);
}
CreatedFunctionResult WriteCreatedFunctionTemplate(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const analysis::FunctionCreation& creation,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings, const abc::ModuleInfo* module,
    const analysis::MethodRoleIndex* roles,
    const GlobalScope* global_scope, CreationExpansion* expansion) {
  return WriteCreation(file, header, index, lookup, caller, creation, output,
                       budget, strings, analysis::CapturedValues::kLanguage,
                       module, roles, global_scope, expansion);
}
}  // namespace abcd::source
