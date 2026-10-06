#include "syntax_resolution.h"
#include "rest_ownership.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "parameter_names.h"
#include "cell_names.h"
#include "abcd/analysis/rest_arguments.h"
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
    FunctionOwnerId owner, analysis::SemanticBudget budget, std::span<const SignatureString> signature) {
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
    auto resolved = ResolveTreeConstants(file, header, index, method.offset, graph, {bytes, work}, signature);
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
    // Method runtime names do not introduce a named-function self binding.
    if (context.CreationSite() && !IsMethodCallable(context.Callable())) {
      Require(syntax.names.size() < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
      reserve(syntax.names, syntax.names.size() + 1);
      const auto name = context.Name();
      step(name.size()); admit(name.size(), sizeof(char16_t)); admit(1, sizeof(char16_t));
      syntax.inferred_name = NameId{static_cast<std::uint32_t>(syntax.names.size())};
      const auto& stored = syntax.names.emplace_back(name);
      admit(stored.capacity() - name.size(), sizeof(char16_t));
    }
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
    std::optional<std::uint16_t> rest_argument;
    for (const auto& op : graph.ops) {
      step(1);
      if (op.life != analysis::ValueLife::kLive) continue;
      const auto* rest = std::get_if<analysis::ValueRestArguments>(&op.operation);
      if (!rest) continue;
      Require(!rest_argument && rest->first_argument == context.Length() &&
          static_cast<std::uint32_t>(rest->first_argument) + 1 == frame.parameters &&
          ResourceFailures(identity.contract) == ResourceFailureScope::kExcluded, Error::kParameters);
      rest_argument = rest->first_argument;
    }
    if (rest_argument) {
      // Hoist only an allocation owned by this argument frame. CFG effects
      // and SSA origins establish this; compiler copy order and local numbers
      // do not define native rest. Independent arrays/scalar slots stay distinct.
      const auto ownership = ProveRestOwnership(graph, body, frame, *rest_argument, {bytes, work});
      if (const auto* error = std::get_if<RestOwnershipError>(&ownership)) {
        if (*error == RestOwnershipError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == RestOwnershipError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kParameters;
      }
      const auto& proof = std::get<RestOwnership>(ownership); admit(proof.allocated_bytes, 1); step(proof.work);
      syntax.parameters[*rest_argument].rest = true;
    }
    // Scope literals name proven storage, never infer storage from spelling.
    // Every callable shares this adapter; record-only naming loses captures in
    // methods/children. Binding scopes and collisions remain tree obligations.
    auto named = ResolveCellNames(file, header, index, method.offset, graph,
        syntax.names, syntax.locals, {bytes, work});
    if (const auto* storage = std::get_if<CellNameStorage>(&named)) {
      admit(storage->bytes, 1); step(storage->work);
    } else if (const auto* error = std::get_if<abc::MetadataError>(&named)) {
      return SyntaxResolutionFailure{*error};
    } else return SyntaxResolutionFailure{std::get<TreeConstantError>(named)};
    if (file.CheckUnchanged()) return SyntaxResolutionFailure{abc::MetadataError::kFileChanged};
    result.allocated_bytes = budget.bytes - bytes;
    result.work = budget.work - work;
    return result;
  } catch (Error error) { return SyntaxResolutionFailure{error};
  } catch (const std::bad_alloc&) { return SyntaxResolutionFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return SyntaxResolutionFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
