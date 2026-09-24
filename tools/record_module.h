#ifndef ABCD_TOOLS_RECORD_MODULE_H_
#define ABCD_TOOLS_RECORD_MODULE_H_

#include <optional>
#include "abcd/abc/record_attributes.h"
#include "abcd/source/module_exports.h"

struct RecordModule {
  abcd::abc::ModuleInfo module;
  abcd::source::ModuleExports exports;
};
enum class RecordModuleError { kAttributes, kModule, kExports };
// Same-file merged record only. Read-only slot planning, never initialization:
// callers may borrow these owners synchronously, but must not call WriteStore
// to manufacture facts for a detached method inspection.
std::variant<std::optional<RecordModule>, RecordModuleError> ReadRecordModule(
    const abcd::corpus::AbcFile& file, const abcd::abc::Header& header,
    const abcd::abc::MetadataIndex& index, const abcd::abc::RecordInfo& record);
#endif
