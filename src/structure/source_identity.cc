#include "record_text.h"

#include "abcd/abc/debug_scopes.h"

namespace abcd::structure {
namespace {
enum class FileEvidence { kMissing, kUnique, kConflicting, kIncomplete };
bool Fatal(abc::MetadataError error) {
  return error == abc::MetadataError::kRead ||
         error == abc::MetadataError::kFileChanged ||
         error == abc::MetadataError::kResourceLimit;
}
}  // namespace

void WriteSourceIdentity(const RecordText& text, const abc::MetadataIndex& index,
                         const abc::RecordInfo& record, std::ostream& out) {
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
  out << ",\"source_file_evidence\":{\"state\":" << static_cast<unsigned>(state)
      << ",\"file\":";
  if (state == FileEvidence::kUnique)
    text.String(out, name);
  else
    out << "null";
  out << '}';
}
}  // namespace abcd::structure
