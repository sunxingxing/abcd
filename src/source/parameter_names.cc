#include "parameter_names.h"

#include <algorithm>
#include <new>
#include <optional>
#include <stdexcept>

#include "abcd/abc/string_entity.h"
#include "abcd/source/global_reference.h"
#include "abcd/source/scalar_expression.h"

namespace abcd::source {
namespace {
std::size_t ScopeBytes(const abc::DebugScopes& scopes) {
  return scopes.locals.capacity() * sizeof(abc::DebugLocal) +
         scopes.positions.capacity() * sizeof(abc::DebugPosition);
}
bool Fatal(abc::MetadataError error) {
  return error == abc::MetadataError::kRead ||
         error == abc::MetadataError::kFileChanged ||
         error == abc::MetadataError::kResourceLimit;
}
bool ValidName(std::u16string_view name, std::string_view self_binding, ParameterNameDomain domain) {
  switch (domain) {
    case ParameterNameDomain::kLegacyWriter: return AllowsGlobalReference(name, {{}, self_binding, {}});
    case ParameterNameDomain::kSourceTree: return IsSourceIdentifier(name);
  }
  return false;
}
}  // namespace

std::size_t RecoveredParameters::AllocatedBytes() const {
  std::size_t bytes = names.capacity() * sizeof(std::string);
  for (const auto& name : names) bytes += name.capacity();
  return bytes;
}

ParameterNameResult RecoverParameterNames(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MethodInfo& method, const abc::FrameLayout& frame,
    const abc::MethodBody& body, std::size_t bytes,
    abc::StringBudget strings, std::string_view self_binding, ParameterNameDomain domain) {
  using Error = ParameterNameError;
  try {
    if (!method.debug_info || !method.code || frame.parameters == 0)
      return Error::kUnproven;
    const auto decoded = abc::ReadDebugInfo(
        file, header, *method.debug_info, {bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
      return Fatal(*error) ? ParameterNameResult{*error}
                           : ParameterNameResult{Error::kUnproven};
    const auto& debug = std::get<abc::DebugInfo>(decoded);
    const auto debug_bytes =
        debug.parameters.capacity() * sizeof(std::optional<abc::EntityOffset>);
    if (debug_bytes > bytes) return Error::kResourceLimit;
    bytes -= debug_bytes;
    const auto scoped = abc::ReadDebugScopes(
        file, header, debug, method.code->instruction_bytes, {bytes});
    if (const auto* error = std::get_if<abc::DebugScopeFailure>(&scoped)) {
      if (const auto* metadata = std::get_if<abc::MetadataError>(&error->error))
        return Fatal(*metadata) ? ParameterNameResult{*metadata}
                                : ParameterNameResult{Error::kUnproven};
      return std::get<abc::DebugScopeError>(error->error) ==
                     abc::DebugScopeError::kResourceLimit
                 ? Error::kResourceLimit
                 : Error::kUnproven;
    }
    const auto& scopes = std::get<abc::DebugScopes>(scoped);
    const auto scope_bytes = ScopeBytes(scopes);
    if (scope_bytes > bytes) return Error::kResourceLimit;
    bytes -= scope_bytes;
    const auto mapped = MapParameterDebugNames(frame, body, scopes, header.profile, bytes);
    if (const auto* error = std::get_if<Error>(&mapped)) return *error;
    const auto& names = std::get<ParameterOffsets>(mapped);
    if (names.capacity() > bytes / sizeof(abc::EntityOffset))
      return Error::kResourceLimit;
    bytes -= names.capacity() * sizeof(abc::EntityOffset);
    if (frame.parameters > bytes / sizeof(std::string))
      return Error::kResourceLimit;
    bytes -= frame.parameters * sizeof(std::string);
    RecoveredParameters result;
    result.names.reserve(frame.parameters);
    if (result.names.capacity() > frame.parameters &&
        result.names.capacity() - frame.parameters >
            bytes / sizeof(std::string))
      return Error::kResourceLimit;
    bytes -= (result.names.capacity() - frame.parameters) *
             sizeof(std::string);
    for (const auto entity : names) {
      const auto parsed = abc::ReadStringEntity(
          file, header, entity, {std::min(bytes, strings.bytes)});
      if (const auto* error = std::get_if<abc::MetadataError>(&parsed))
        return Fatal(*error) ? ParameterNameResult{*error}
                             : ParameterNameResult{Error::kUnproven};
      const auto& name = std::get<std::u16string>(parsed);
      if (!ValidName(name, self_binding, domain)) return Error::kUnproven;
      const auto transient = name.capacity() * sizeof(char16_t);
      if (transient > bytes || name.size() > bytes - transient)
        return Error::kResourceLimit;
      auto& stored = result.names.emplace_back();
      stored.reserve(name.size());
      for (const auto ch : name) stored.push_back(static_cast<char>(ch));
      if (stored.capacity() > bytes - transient)
        return Error::kResourceLimit;
      bytes -= stored.capacity();
    }
    if (result.names.size() > bytes / sizeof(std::size_t))
      return Error::kResourceLimit;
    std::vector<std::size_t> order(result.names.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return result.names[a] < result.names[b];
    });
    for (std::size_t i = 1; i < order.size(); ++i)
      if (result.names[order[i - 1]] == result.names[order[i]])
        return Error::kUnproven;
    return result;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
