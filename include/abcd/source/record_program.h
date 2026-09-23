#ifndef ABCD_SOURCE_RECORD_PROGRAM_H_
#define ABCD_SOURCE_RECORD_PROGRAM_H_

#include "abcd/analysis/method_roles.h"
#include "abcd/source/created_class.h"
#include "abcd/source/module_exports.h"

namespace abcd::source {
enum class RecordProgramError {
  kProfile,
  kEntry,
  kModule,
  kBody,
  kFrame,
  kInstruction,
  kValue,
  kClass,
  kExport,
  kResourceLimit,
  kOutputLimit,
  kRead,
  kWrite,
  kFileChanged
};
struct RecordProgramFailure {
  RecordProgramError kind;
  abc::CodeOffset pc;
};
struct RecordProgramBudget {
  std::size_t bytes = 128 * 1024 * 1024;
  std::size_t export_bytes = 16 * 1024 * 1024;
  EmitBudget output;
  abc::StringBudget strings;
};
using RecordProgramResult =
    std::variant<std::size_t, RecordProgramFailure>;

// Emits one complete ESM record only when every entry instruction and all
// transitively created class bodies pass source admission. The caller supplies
// an unchanged merged-module record, its own module table and whole-file role
// index. Current semantic profile is pinned v7 dynamic 13.0.1.0; older ABC
// containers remain parseable but require separate execution proofs here.
// Semantic/budget refusal writes nothing. IO/file mutation during the second
// pass can leave a prefix; publish only through a staged atomic file.
[[nodiscard]] RecordProgramResult WriteRecordProgram(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const abc::ModuleInfo& module,
    const analysis::MethodRoleIndex& roles, std::ostream& output,
    RecordProgramBudget budget = {});
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_PROGRAM_H_
