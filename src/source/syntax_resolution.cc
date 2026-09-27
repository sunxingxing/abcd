#include "syntax_resolution.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "parameter_names.h"
#include "abcd/corpus/abc_file.h"

namespace abcd::source {
namespace {
using Error = SyntaxResolutionError;
void Require(bool condition, Error error) { if (!condition) throw error; }
}  // namespace
std::variant<ResolvedSyntax, SyntaxResolutionFailure> ResolveFunctionSyntax(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const FunctionContext& context,
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    FunctionOwnerId owner, analysis::SemanticBudget budget) {
  try {
    auto bytes = budget.bytes;
    auto work = budget.work;
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= bytes / width, Error::kResourceLimit); bytes -= count * width;
    };
    const auto step = [&](std::uint64_t count) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    const auto reserve = [&]<class T>(std::vector<T>& values, std::size_t count) {
      if (count <= values.capacity()) return;
      // Keep the old arena admitted while reserve may allocate its replacement.
      admit(count, sizeof(T)); values.reserve(count); admit(values.capacity() - count, sizeof(T));
    };
    const auto& identity = context.Identity();
    Require(context.Position().value < index.methods.size() &&
        index.methods[context.Position().value].offset == context.Target() &&
        identity.profile == header.profile && identity.abc_version == header.version &&
        identity.file_size == header.file_size, Error::kContext);
    const auto& method = index.methods[context.Position().value];
    admit(1, sizeof(ResolvedSyntax));
    auto resolved = ResolveTreeConstants(file, header, index, method.offset, graph, {bytes, work});
    if (const auto* error = std::get_if<TreeConstantError>(&resolved)) return SyntaxResolutionFailure{*error};
    if (const auto* error = std::get_if<abc::MetadataError>(&resolved)) return SyntaxResolutionFailure{*error};
    ResolvedSyntax result;
    auto constants = std::get<TreeConstants>(std::move(resolved));
    admit(constants.allocated_bytes, 1); step(constants.work);
    result.value_revision = constants.value_revision; result.cfg_revision = constants.cfg_revision;
    result.operations = std::move(constants.operations);
    result.number_type_text = constants.number_type_text;
    result.properties = std::move(constants.properties);
    auto& syntax = result.syntax;
    syntax.identity = identity; syntax.method = method.offset; syntax.owner = owner;
    syntax.callable = context.Callable(); syntax.strictness = context.Mode();
    syntax.names = std::move(constants.names);
    syntax.texts = std::move(constants.texts);
    const auto& frame = context.Frame();
    reserve(syntax.parameters, frame.parameters);
    step(frame.parameters); syntax.parameters.resize(frame.parameters);
    if (frame.parameters != 0) {
      const auto recovered = RecoverParameterNames(file, header, method, frame, body, bytes);
      if (const auto* error = std::get_if<abc::MetadataError>(&recovered)) {
        if (*error == abc::MetadataError::kResourceLimit || *error == abc::MetadataError::kFileChanged)
          return SyntaxResolutionFailure{*error};
        return SyntaxResolutionFailure{Error::kParameters};
      }
      if (const auto* error = std::get_if<ParameterNameError>(&recovered)) {
        // Lack of debug proof keeps synthesized names, never guessed register
        // aliases. Resource failure is not permission to change the naming plan.
        switch (*error) {
          case ParameterNameError::kUnproven: break;
          case ParameterNameError::kResourceLimit: throw Error::kResourceLimit;
        }
      } else {
        const auto& parameters = std::get<RecoveredParameters>(recovered);
        Require(parameters.names.size() == frame.parameters, Error::kParameters);
        admit(parameters.names.capacity(), sizeof(std::string));
        for (const auto& name : parameters.names) { step(1); admit(name.capacity(), 1); admit(1, 1); }
        constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
        Require(syntax.names.size() <= kMaxId - frame.parameters, Error::kResourceLimit);
        reserve(syntax.names, syntax.names.size() + frame.parameters);
        for (std::size_t i = 0; i < parameters.names.size(); ++i) {
          const auto& name = parameters.names[i];
          step(name.size()); admit(name.size(), sizeof(char16_t)); admit(1, sizeof(char16_t));
          const NameId id{static_cast<std::uint32_t>(syntax.names.size())};
          auto& owned = syntax.names.emplace_back(name.begin(), name.end());
          admit(owned.capacity() - name.size(), sizeof(char16_t));
          syntax.parameters[i] = {id, NameEvidence::kDebugRange};
        }
      }
    }
    if (file.CheckUnchanged()) return SyntaxResolutionFailure{abc::MetadataError::kFileChanged};
    result.allocated_bytes = budget.bytes - bytes;
    result.work = budget.work - work;
    return result;
  } catch (Error error) { return SyntaxResolutionFailure{error};
  } catch (const std::bad_alloc&) { return SyntaxResolutionFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return SyntaxResolutionFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
