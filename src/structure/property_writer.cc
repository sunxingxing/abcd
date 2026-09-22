#include "abcd/analysis/property_receiver.h"
#include "abcd/analysis/property_write.h"
#include "record_text.h"

namespace abcd::structure {
namespace {
const char* Kind(analysis::PropertyWriteKind kind) {
  switch (kind) {
    case analysis::PropertyWriteKind::kAssignment:
      return "assignment";
    case analysis::PropertyWriteKind::kOwnStore:
      return "own_store";
    case analysis::PropertyWriteKind::kDefineField:
      return "define_field";
  }
  std::terminate();
}
}  // namespace
void WritePropertySites(const RecordText& text, const abc::MetadataIndex& index,
                        const abc::MethodInfo& method,
                        const abc::MethodBody& body, std::ostream& out,
                        const abc::FrameLayout* frame) {
  std::vector<analysis::PropertyReceiver> receivers;
  if (frame) {
    auto proof = analysis::ProvePropertyReceivers(body, frame->registers,
                                                  frame->this_value,
                                                  {text.budget.receiver_bytes});
    if (const auto* error = std::get_if<analysis::ReceiverError>(&proof)) {
      out << ",\"receiver_scan_error\":" << static_cast<unsigned>(*error);
    } else
      receivers =
          std::get<std::vector<analysis::PropertyReceiver>>(std::move(proof));
  }
  std::size_t receiver_index = 0;
  out << ",\"property_writes\":[";
  bool comma = false;
  const auto frame_size = static_cast<std::uint64_t>(method.code->registers) +
                          method.code->arguments;
  for (const auto& instruction : body.instructions) {
    const auto lifted =
        analysis::LiftPropertyWrite(instruction.decoded, frame_size);
    if (const auto* error =
            std::get_if<analysis::PropertyWriteError>(&lifted)) {
      if (*error == analysis::PropertyWriteError::kUnsupportedOpcode) continue;
      throw StructureFailure{method.offset, StructureFault::kBindings};
    }
    if (comma) out << ',';
    comma = true;
    out << "{\"pc\":" << instruction.offset.value;
    const auto& write = std::get<analysis::PropertyWrite>(lifted);
    const auto origin = receivers.empty() ? analysis::ReceiverOrigin::kUnknown
                                          : receivers[receiver_index].origin;
    ++receiver_index;
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
      out << ",\"kind\":\"" << Kind(write.kind)
          << "\",\"object_register\":" << write.object.value << ",\"name\":";
      text.String(out, name);
      // Receiver identity is not proven by the opcode or property name.
      out << ",\"instance\":null";
      out << ",\"receiver_origin\":\""
          << (origin == analysis::ReceiverOrigin::kEntryThis ? "entry_this"
                                                             : "unknown")
          << '\"';
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
