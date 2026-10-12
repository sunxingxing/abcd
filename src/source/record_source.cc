#include "abcd/source/record_source.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "record_tree.h"

namespace abcd::source {
RecordSourceResult BuildRecordSource(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const RecordModuleContext& context,
    const analysis::MethodRoleIndex& roles, RecordProgramBudget budget,
    const RouteInputIdentity& identity,
    const analysis::FunctionCreationSites* functions, const analysis::ClassMethodSites* classes) {
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
    const auto position = lookup.Find(entry->offset);
    if (!position) return fail(Error::kEntry);
    return BuildRecordTree(file, header, index, record, context, *position, identity,
                           budget, lookup, roles, functions, classes);
  } catch (const RecordProgramFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return fail(Error::kAllocationFailure);
  } catch (const std::length_error&) {
    return fail(Error::kAllocationFailure);
  }
}
}  // namespace abcd::source
