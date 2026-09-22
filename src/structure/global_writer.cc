#include "abcd/analysis/global_access.h"
#include "record_text.h"

namespace abcd::structure {
namespace {
const char* Operation(analysis::GlobalOp operation) {
  using analysis::GlobalOp;
  switch (operation) {
    case GlobalOp::kReadName:
      return "name_read";
    case GlobalOp::kReadVar:
      return "var_read";
    case GlobalOp::kWriteName:
      return "name_write";
    case GlobalOp::kWriteVar:
      return "var_write";
    case GlobalOp::kInitConst:
      return "const_record_init";
    case GlobalOp::kInitMutable:
      return "mutable_record_init";
  }
  std::terminate();
}
}  // namespace
void WriteGlobalSites(const RecordText& text, const abc::MetadataIndex& index,
                      const abc::MethodInfo& method,
                      const abc::MethodBody& body, std::ostream& out) {
  out << ",\"global_accesses\":[";
  bool comma = false;
  for (const auto& instruction : body.instructions) {
    const auto lifted = analysis::LiftGlobalAccess(instruction.decoded);
    if (const auto* error = std::get_if<analysis::GlobalAccessError>(&lifted)) {
      if (*error == analysis::GlobalAccessError::kUnsupportedOpcode) continue;
      throw StructureFailure{method.offset, StructureFault::kBindings};
    }
    if (comma) out << ',';
    comma = true;
    out << "{\"pc\":" << instruction.offset.value;
    const auto& access = std::get<analysis::GlobalAccess>(lifted);
    try {
      const auto reference = abc::ResolvePoolReference(
          text.file, text.header, index, method.offset, access.name,
          abc::PoolEntityKind::kString);
      if (const auto* error = std::get_if<abc::MetadataError>(&reference))
        throw StructureFailure{method.offset, *error};
      // Preflight lazy names before site payload. Neither loads nor Var-family
      // stores prove a source declaration or an object-only binding domain.
      const auto name =
          text.Read(std::get<abc::PoolReference>(reference).offset);
      out << ",\"operation\":\"" << Operation(access.operation)
          << "\",\"name\":";
      text.String(out, name);
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
