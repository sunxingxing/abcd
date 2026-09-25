#include "abcd/source/record_source.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "abcd/abc/call_type.h"
#include "import_names.h"

namespace abcd::source {
RecordSourceResult BuildRecordSource(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const RecordModuleContext& context,
    const analysis::MethodRoleIndex& roles, RecordProgramBudget budget,
    const HostRealmBinding* realm_binding) {
  using Error = RecordProgramError;
  const auto fail = [](Error kind) { return RecordProgramFailure{kind, {}}; };
  const auto metadata = [&](abc::MetadataError kind) {
    switch (kind) {
      case abc::MetadataError::kResourceLimit: return fail(Error::kResourceLimit);
      case abc::MetadataError::kRead: return fail(Error::kRead);
      case abc::MetadataError::kFileChanged: return fail(Error::kFileChanged);
      default: return fail(Error::kModule);
    }
  };
  try {
    if (header.kind != abc::AbcKind::kDynamic ||
        !SupportsRecordProgramProfile(header.profile)) return fail(Error::kProfile);
    const auto module = context.ForOwner(file, record.offset);
    if (!module) return fail(Error::kModule);
    const auto attributes = abc::ReadMergedRecordAttributes(file, header, index,
        record, {std::min(budget.strings.bytes, budget.bytes)});
    const auto* flags = std::get_if<abc::RecordAttributes>(&attributes);
    if (!flags) {
      const auto& error = std::get<abc::AttributeFailure>(attributes);
      if (error.metadata_error) return metadata(*error.metadata_error);
      return fail(error.kind == abc::AttributeError::kFileChanged
          ? Error::kFileChanged : Error::kModule);
    }
    if (flags->common_js.value_or(false) || flags->shared.value_or(false) ||
        flags->top_level_await.value_or(false) || flags->json_content)
      return fail(Error::kModule);
    if (record.first_method > index.methods.size() ||
        record.method_count > index.methods.size() - record.first_method)
      return fail(Error::kEntry);
    const abc::MethodInfo* entry = nullptr;
    // Pinned PandaFileTranslator record-entry wire name, not semantic dispatch.
    constexpr std::string_view kEntryName = "func_main_0";
    for (const auto& method : std::span(index.methods).subspan(
             record.first_method, record.method_count)) {
      if (method.owner_offset != record.offset) return fail(Error::kEntry);
      if (method.name != kEntryName) continue;
      if (entry) return fail(Error::kEntry);
      entry = &method;
    }
    if (!entry || !entry->code) return fail(Error::kEntry);
    const auto call = abc::ReadCallType(file, header, index, entry->annotations,
                                       {budget.bytes});
    const auto* call_type = std::get_if<abc::CallType>(&call);
    if (!call_type) return metadata(std::get<abc::MetadataError>(call));
    const auto framed = abc::BuildFrameLayout(entry->code->registers,
        entry->code->arguments, abc::DecodeCallFlags(call_type->value));
    const auto* frame = std::get_if<abc::FrameLayout>(&framed);
    if (!frame || frame->parameters != 0) return fail(Error::kFrame);
    const auto decoded = abc::DecodeMethodBody(file, header.profile,
                                               *entry->code, {budget.bytes});
    const auto* body = std::get_if<abc::MethodBody>(&decoded);
    if (!body) {
      const auto& error = std::get<abc::BodyFailure>(decoded);
      return RecordProgramFailure{error.kind == abc::BodyError::kResourceLimit
          ? Error::kResourceLimit : error.kind == abc::BodyError::kFile
          ? Error::kRead : Error::kBody, error.offset};
    }
    const auto retain = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) throw fail(Error::kResourceLimit);
      budget.bytes -= count * width;
    };
    retain(body->instructions.capacity(), sizeof(abc::BodyInstruction));
    retain(body->tries.capacity(), sizeof(abc::TryRegion));
    retain(body->handlers.capacity(), sizeof(abc::CatchHandler));
    // Export planning has a bounded retained allocation plus string scratch.
    // Reserve both alongside the live body before building IR and proof state.
    retain(budget.export_bytes, 1);
    retain(budget.strings.bytes, 1);
    retain(budget.export_bytes, 1);
    auto named = RecoverImportNames(file, header, *module,
        std::span(index.methods).subspan(record.first_method, record.method_count),
        budget.export_bytes, budget.strings);
    const auto* imports = std::get_if<ImportNames>(&named);
    if (!imports) {
      const auto& error = std::get<ModuleImportFailure>(named);
      return fail(error.kind == ModuleImportError::kResourceLimit
          ? Error::kResourceLimit : Error::kModule);
    }
    auto planned = BuildModuleExports(file, header, *module,
                                      {budget.export_bytes, budget.strings});
    auto* exports = std::get_if<ModuleExports>(&planned);
    if (!exports) {
      if (const auto* error = std::get_if<abc::MetadataError>(&planned))
        return metadata(*error);
      return fail(std::get<ModuleExportError>(planned) == ModuleExportError::kResourceLimit
          ? Error::kResourceLimit : Error::kExport);
    }
    const auto lifted = analysis::BuildSemanticMethod(*body, header.profile,
                                                     frame->registers, {budget.bytes});
    const auto* method = std::get_if<analysis::SemanticMethod>(&lifted);
    if (!method) return std::get<analysis::SemanticFailure>(lifted);
    MethodConstants constants{file, header, index, entry->offset, budget.strings};
    constants.leaf_methods = &lookup;
    constants.roles = &roles;
    constants.module = module.get();
    constants.global_scope.emplace();
    constants.global_scope->module_slots = exports->slot_count();
    constants.global_scope->module_exports = exports;
    constants.global_scope->realm_binding = realm_binding;
    constants.global_scope->import_bindings = imports->slots;
    constants.global_scope->import_names = imports->sorted;
    auto result = BuildEntrySource(*body, *method, *frame, SourceForm::kRecord,
        entry->name, constants, exports, {budget.bytes}, budget.output.bytes);
    if (file.CheckUnchanged()) return fail(Error::kFileChanged);
    return std::visit([](auto&& value) -> RecordSourceResult {
      return std::move(value);
    }, std::move(result));
  } catch (const RecordProgramFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return fail(Error::kResourceLimit);
  } catch (const std::length_error&) {
    return fail(Error::kResourceLimit);
  }
}
}  // namespace abcd::source
