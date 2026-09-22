#ifndef ABCD_STRUCTURE_RECORD_WRITER_H_
#define ABCD_STRUCTURE_RECORD_WRITER_H_

#include <ostream>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/abc/module_info.h"
#include "abcd/abc/record_attributes.h"
#include "abcd/source/string_literal.h"

namespace abcd::structure {
enum class StructureFault {
  kMemberRange,
  kOwner,
  kBindings,
  kResourceLimit,
  kOutputLimit,
  kWrite
};
struct StructureFailure {
  abc::EntityOffset entity;
  std::variant<StructureFault, abc::MetadataError, abc::AttributeFailure,
               abc::ModuleFailure, abc::FrameError, abc::MethodAttributeError,
               source::EmitError>
      error;
};
struct RecordCounts {
  std::size_t methods = 0;
  std::size_t fields = 0;
  std::size_t requests = 0;
  std::size_t imports = 0;
  std::size_t exports = 0;
};
struct StructureBudget {
  std::size_t module_bytes = 64 * 1024 * 1024;
  std::size_t binding_bytes = 32 * 1024 * 1024;
  std::size_t string_bytes = 1024 * 1024;
  std::size_t output_bytes = 64 * 1024 * 1024;
};
// Stream ONE unchanged indexed record. Outputs are declaration-only evidence,
// not executable decompilation. Caller stages both files and publishes only
// on success. Source-class membership and local scopes are not inferred from
// the record table. Bundle module ownership remains explicitly unresolved.
[[nodiscard]] std::variant<RecordCounts, StructureFailure> WriteRecordStructure(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    abc::FilePackaging packaging, std::ostream& metadata,
    std::ostream& declarations, StructureBudget budget = {});
}  // namespace abcd::structure
#endif
