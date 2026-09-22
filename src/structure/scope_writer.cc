#include "abcd/abc/debug_scopes.h"
#include "record_text.h"

namespace abcd::structure {
namespace {
void WriteFailure(const abc::DebugScopeFailure& failure, std::ostream& out) {
  if (const auto* error = std::get_if<abc::MetadataError>(&failure.error)) {
    if (*error == abc::MetadataError::kRead ||
        *error == abc::MetadataError::kFileChanged)
      throw StructureFailure{failure.offset, *error};
  }
  out << "null,\"scope_error\":{\"offset\":" << failure.offset.value;
  if (const auto* error = std::get_if<abc::MetadataError>(&failure.error))
    out << ",\"domain\":\"metadata\",\"code\":"
        << static_cast<unsigned>(*error);
  else
    out << ",\"domain\":\"debug_scope\",\"code\":"
        << static_cast<unsigned>(std::get<abc::DebugScopeError>(failure.error));
  out << '}';
}
void ValidateStrings(const RecordText& text, const abc::DebugScopes& scopes) {
  // Preflight lazy payloads before opening the JSON object. One decoded string
  // at a time; no output-sized buffer or unbounded string cache is needed.
  for (const auto& local : scopes.locals) {
    text.Read(local.name);
    text.Read(local.type);
    if (local.signature) text.Read(*local.signature);
  }
  for (const auto& position : scopes.positions)
    if (position.file) text.Read(*position.file);
  if (scopes.terminal_file) text.Read(*scopes.terminal_file);
}
}  // namespace
void WriteScopeStructure(const RecordText& text, const abc::MethodInfo& method,
                         const abc::DebugInfo& debug, std::ostream& out) {
  out << ",\"scopes\":";
  if (!method.code) {
    out << "null";
    return;
  }
  const auto decoded = abc::ReadDebugScopes(
      text.file, text.header, debug, method.code->instruction_bytes,
      {text.budget.scope_bytes, text.budget.scope_events});
  if (const auto* failure = std::get_if<abc::DebugScopeFailure>(&decoded)) {
    // Unusable debug data must not erase validated record/module evidence.
    // Failure remains explicit; callers may not treat it as an empty scope set.
    WriteFailure(*failure, out);
    return;
  }
  const auto& scopes = std::get<abc::DebugScopes>(decoded);
  try {
    ValidateStrings(text, scopes);
  } catch (const StructureFailure& failure) {
    const auto* error = std::get_if<abc::MetadataError>(&failure.error);
    if (!error) throw;
    WriteFailure({failure.entity, *error}, out);
    return;
  }
  out << "{\"terminal_pc\":" << scopes.terminal_pc << ",\"locals\":[";
  bool comma = false;
  for (const auto& local : scopes.locals) {
    if (comma) out << ',';
    comma = true;
    out << "{\"register\":" << local.reg << ",\"name\":";
    text.String(out, local.name);
    out << ",\"debug_type\":";
    text.String(out, local.type);
    out << ",\"debug_signature\":";
    if (local.signature)
      text.String(out, *local.signature);
    else
      out << "null";
    out << ",\"start\":" << local.start << ",\"end\":";
    if (local.end)
      out << *local.end;
    else
      out << "null";
    out << '}';
  }
  out << "],\"positions\":[";
  comma = false;
  for (const auto& position : scopes.positions) {
    if (comma) out << ',';
    comma = true;
    out << "{\"pc\":" << position.pc << ",\"line\":";
    if (position.line)
      out << *position.line;
    else
      out << "null";
    out << ",\"column\":" << position.column << ",\"file\":";
    if (position.file)
      text.String(out, *position.file);
    else
      out << "null";
    out << ",\"source_code_ref\":";
    if (position.source_code)
      out << position.source_code->value;
    else
      out << "null";
    out << '}';
  }
  out << "],\"terminal_file\":";
  if (scopes.terminal_file)
    text.String(out, *scopes.terminal_file);
  else
    out << "null";
  out << ",\"terminal_source_code_ref\":";
  if (scopes.terminal_source_code)
    out << scopes.terminal_source_code->value;
  else
    out << "null";
  out << '}';
}
}  // namespace abcd::structure
