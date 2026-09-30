#include "function_file.h"

#include <new>
#include <stdexcept>
#include "abcd/abc/call_type.h"

namespace abcd::source {
std::variant<FileFunction, FunctionFileFailure> ReadFunctionValues(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    abc::MethodPosition position, RouteInputIdentity identity, InputCaptureMode mode,
    analysis::SemanticBudget budget, const RecordModuleContext* modules, const FunctionContext* owner) {
  using Error = FunctionFileError;
  try {
    auto bytes = budget.bytes;
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > bytes / width) throw Error::kResourceLimit;
      bytes -= count * width;
    };
    const auto arena = [&]<class T>(const std::vector<T>& values) { admit(values.capacity(), sizeof(T)); };
    if (identity.profile != header.profile || identity.abc_version != header.version ||
        identity.file_size != header.file_size) throw Error::kIdentity;
    if (file.Size() != header.file_size || file.CheckUnchanged()) throw Error::kChanged;
    if (position.value >= index.methods.size() || !index.methods[position.value].code) throw Error::kMethod;
    const auto& method = index.methods[position.value];
    admit(1, sizeof(FileFunction));
    const auto call = abc::ReadCallType(file, header, index, method.annotations, {bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&call)) return FunctionFileFailure{*error};
    const auto framed = abc::BuildFrameLayout(method.code->registers, method.code->arguments,
        abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
    if (const auto* error = std::get_if<abc::FrameError>(&framed)) return FunctionFileFailure{*error};
    const auto frame = std::get<abc::FrameLayout>(framed);
    auto decoded = abc::DecodeMethodBody(file, header.profile, *method.code, {bytes});
    if (const auto* error = std::get_if<abc::BodyFailure>(&decoded)) return FunctionFileFailure{*error};
    auto body = std::get<abc::MethodBody>(std::move(decoded));
    arena(body.instructions); arena(body.tries); arena(body.handlers);
    auto proven = FunctionInput::Build(identity, method.offset, method.owner_offset, body, frame, mode,
        {bytes, budget.work}, owner);
    if (const auto* error = std::get_if<FunctionInputFailure>(&proven)) return FunctionFileFailure{*error};
    auto input = std::get<FunctionInput>(std::move(proven));
    admit(input.AllocatedBytes(), 1);
    auto prepared = BuildFunctionValues(input, body, frame, {bytes, budget.work});
    if (const auto* error = std::get_if<FunctionValueFailure>(&prepared)) return FunctionFileFailure{*error};
    auto values = std::get<FunctionValues>(std::move(prepared));
    const auto& graph = values.graph;
    arena(graph.blocks); arena(graph.edges); arena(graph.ops); arena(graph.values); arena(graph.uses);
    arena(graph.origins); arena(graph.exceptional_exits); arena(graph.input.sites); arena(graph.input.values);
    if (values.environments) admit(values.environments->allocated_bytes, 1);
    if (values.cells) admit(values.cells->allocated_bytes, 1);
    admit(values.modules.allocated_bytes, 1);
    admit(values.classes.allocated_bytes, 1);
    admit(values.initializers.allocated_bytes, 1);
    std::optional<ModuleReads> module_reads;
    if (modules) {
      if (modules->record() != method.owner_offset) throw Error::kIdentity;
      const auto module = modules->ForOwner(file, method.owner_offset);
      auto resolved = ResolveModuleReads(file, header, module.get(), graph, {bytes, budget.work});
      if (const auto* error = std::get_if<ModuleReadFailure>(&resolved)) return FunctionFileFailure{*error};
      module_reads = std::get<ModuleReads>(std::move(resolved));
      admit(module_reads->allocated_bytes, 1);
    }
    if (file.CheckUnchanged()) throw Error::kChanged;
    return FileFunction{std::move(body), frame, std::move(input), std::move(values),
        budget.bytes - bytes, std::move(module_reads)};
  } catch (Error error) { return FunctionFileFailure{error};
  } catch (const std::bad_alloc&) { return FunctionFileFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FunctionFileFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
