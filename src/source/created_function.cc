#include "abcd/source/created_function.h"

#include "abcd/abc/call_type.h"

namespace abcd::source {
namespace {
constexpr unsigned char kAsciiLimit = 0x80;
}  // namespace
CreatedFunctionResult WriteCreatedFunction(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings) {
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
  if (!method.code) return CreatedFunctionError::kMissingCode;

  // Pinned MethodLiteral::ParseFunctionNameView and JSPandaFile::GetMethodName.
  // Non-ASCII uses a UTF-16-length byte view upstream: do not silently replace
  // that behavior with Unicode-aware substring processing. No copy is needed.
  std::string_view name = method.name;
  if (name.empty() || name.size() != method.name_utf16_length)
    return CreatedFunctionError::kName;
  for (const unsigned char byte : name)
    if (byte == 0 || byte >= kAsciiLimit) return CreatedFunctionError::kName;
  if (name.front() == '#') {
    name.remove_prefix(name.find_last_of('#') + 1);
    const auto suffix = name.find_last_of('^');
    if (suffix != std::string_view::npos) name = name.substr(0, suffix);
  }
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
  const MethodConstants constants{file, header, index, method.offset, strings};
  const auto emitted = WriteBaseFunction(
      {creation, target.attributes, std::get<abc::FrameLayout>(frame), name},
      body, output, budget, &constants);
  return std::visit([](auto value) -> CreatedFunctionResult { return value; },
                    emitted);
}
}  // namespace abcd::source
