#include "record_text.h"

#include <new>
#include <stdexcept>

#include "abcd/abc/debug_scopes.h"
#include "abcd/structure/source_identity.h"

namespace abcd::structure {
namespace {
using FileEvidence = SourceIdentityState;
bool Fatal(abc::MetadataError error) {
  return error == abc::MetadataError::kRead ||
         error == abc::MetadataError::kFileChanged ||
         error == abc::MetadataError::kResourceLimit;
}
SourceIdentity Gather(const RecordText& text, const abc::MetadataIndex& index,
                      const abc::RecordInfo& record) {
  if (record.first_method > index.methods.size() ||
      record.method_count > index.methods.size() - record.first_method)
    throw StructureFailure{record.offset, StructureFault::kMemberRange};
  FileEvidence state = FileEvidence::kMissing;
  std::u16string name;
  std::optional<abc::EntityOffset> previous;
  const auto observe = [&](std::optional<abc::EntityOffset> file) {
    if (!file || previous == file) return;
    auto value = text.Read(*file);
    previous = file;
    if (value.empty()) return;
    if (state == FileEvidence::kMissing) {
      name = std::move(value);
      state = FileEvidence::kUnique;
    } else if (value != name) {
      state = FileEvidence::kConflicting;
    }
  };
  try {
    observe(record.source_file);
    for (std::size_t i = 0; i < record.method_count; ++i) {
      const auto& method = index.methods[record.first_method + i];
      if (method.owner_offset != record.offset)
        throw StructureFailure{method.offset, StructureFault::kOwner};
      if (!method.debug_info || !method.code) continue;
      const auto decoded = abc::ReadDebugInfo(
          text.file, text.header, *method.debug_info, {text.budget.debug_bytes});
      if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
        throw StructureFailure{*method.debug_info, *error};
      const auto scopes = abc::ReadDebugScopes(
          text.file, text.header, std::get<abc::DebugInfo>(decoded),
          method.code->instruction_bytes,
          {text.budget.scope_bytes, text.budget.scope_events});
      if (const auto* failure = std::get_if<abc::DebugScopeFailure>(&scopes)) {
        if (const auto* error = std::get_if<abc::MetadataError>(&failure->error))
          throw StructureFailure{failure->offset, *error};
        state = FileEvidence::kIncomplete;
        break;
      }
      const auto& positions = std::get<abc::DebugScopes>(scopes);
      // Only filenames are inputs. Never dereference embedded source-code
      // references or open the named files. One method payload stays live.
      for (const auto& position : positions.positions) observe(position.file);
      observe(positions.terminal_file);
    }
  } catch (const StructureFailure& failure) {
    const auto* error = std::get_if<abc::MetadataError>(&failure.error);
    if (!error || Fatal(*error)) throw;
    state = FileEvidence::kIncomplete;
  }
  return {state, state == FileEvidence::kUnique ? std::move(name) : std::u16string{}};
}
}  // namespace

SourceIdentityResult ReadSourceIdentity(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    StructureBudget budget) {
  try {
    return Gather(RecordText(file, header, budget), index, record);
  } catch (const StructureFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return StructureFailure{record.offset, StructureFault::kResourceLimit};
  } catch (const std::length_error&) {
    return StructureFailure{record.offset, StructureFault::kResourceLimit};
  }
}

void WriteSourceIdentity(const RecordText& text, const abc::MetadataIndex& index,
                         const abc::RecordInfo& record, std::ostream& out) {
  const auto result = ReadSourceIdentity(text.file, text.header, index, record, text.budget);
  if (const auto* failure = std::get_if<StructureFailure>(&result)) throw *failure;
  const auto& identity = std::get<SourceIdentity>(result);
  out << ",\"source_file_evidence\":{\"state\":" << static_cast<unsigned>(identity.state)
      << ",\"file\":";
  if (identity.state == FileEvidence::kUnique)
    text.String(out, identity.file);
  else
    out << "null";
  out << '}';
}
}  // namespace abcd::structure
