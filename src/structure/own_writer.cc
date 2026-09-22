#include "abcd/analysis/own_write.h"
#include "record_text.h"

namespace abcd::structure {
void WriteOwnSites(const RecordText& text, const abc::MetadataIndex& index,
                   const abc::MethodInfo& method, const abc::MethodBody& body,
                   std::ostream& out) {
  out << ",\"property_writes\":[";
  bool comma = false;
  const auto frame_size = static_cast<std::uint64_t>(method.code->registers) +
                          method.code->arguments;
  for (const auto& instruction : body.instructions) {
    const auto lifted = analysis::LiftOwnWrite(instruction.decoded, frame_size);
    if (const auto* error = std::get_if<analysis::OwnWriteError>(&lifted)) {
      if (*error == analysis::OwnWriteError::kUnsupportedOpcode) continue;
      throw StructureFailure{method.offset, StructureFault::kBindings};
    }
    if (comma) out << ',';
    comma = true;
    out << "{\"pc\":" << instruction.offset.value;
    const auto& write = std::get<analysis::OwnWrite>(lifted);
    try {
      const auto reference = abc::ResolvePoolReference(
          text.file, text.header, index, method.offset, write.name,
          abc::PoolEntityKind::kString);
      if (const auto* error = std::get_if<abc::MetadataError>(&reference))
        throw StructureFailure{method.offset, *error};
      // Read lazy text before emitting payload so a bad name remains
      // site-local.
      const auto name =
          text.Read(std::get<abc::PoolReference>(reference).offset);
      out << ",\"kind\":\""
          << (write.kind == analysis::OwnWriteKind::kStore ? "own_store"
                                                           : "define_field")
          << "\",\"object_register\":" << write.object.value << ",\"name\":";
      text.String(out, name);
      // Receiver identity is not proven by the opcode or property name.
      out << ",\"instance\":null";
    } catch (const StructureFailure& failure) {
      const auto* error = std::get_if<abc::MetadataError>(&failure.error);
      if (!error || *error == abc::MetadataError::kRead ||
          *error == abc::MetadataError::kFileChanged)
        throw;
      out << ",\"error\":{\"domain\":\"metadata\",\"code\":"
          << static_cast<unsigned>(*error) << '}';
    }
    out << '}';
  }
  out << ']';
}
}  // namespace abcd::structure
