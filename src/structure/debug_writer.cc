#include "abcd/abc/debug_info.h"
#include "record_text.h"

namespace abcd::structure {
void WriteDebugStructure(const RecordText& text, const abc::MethodInfo& method,
                         std::ostream& out) {
  if (!method.debug_info) {
    out << "null";
    return;
  }
  const auto decoded = abc::ReadDebugInfo(
      text.file, text.header, *method.debug_info, {text.budget.debug_bytes});
  if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
    throw StructureFailure{*method.debug_info, *error};
  const auto& debug = std::get<abc::DebugInfo>(decoded);
  // Preserve encoded debug ordering without asserting a mapping to ABI or
  // source formals. Missing names and genuinely empty names remain distinct.
  out << "{\"line_start\":" << debug.line_start << ",\"parameter_names\":[";
  bool comma = false;
  for (const auto name : debug.parameters) {
    if (comma) out << ',';
    comma = true;
    if (name)
      text.String(out, *name);
    else
      out << "null";
  }
  out << "],\"constant_pool\":{\"offset\":" << debug.constant_pool.value
      << ",\"bytes\":" << debug.constant_pool_bytes
      << "},\"line_program\":" << debug.line_program.value;
  WriteScopeStructure(text, method, debug, out);
  out << '}';
}
}  // namespace abcd::structure
