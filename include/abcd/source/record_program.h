#ifndef ABCD_SOURCE_RECORD_PROGRAM_H_
#define ABCD_SOURCE_RECORD_PROGRAM_H_

#include "abcd/abc/header.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/emission.h"

namespace abcd::source {
enum class RecordProgramError : unsigned {
  kProfile = 0,
  kEntry = 1,
  kModule = 2,
  kBody = 3,
  kFrame = 4,
  kInstruction = 5,
  kValue = 6,
  kClass = 7,
  kExport = 8,
  kResourceLimit = 9,
  kOutputLimit = 10,
  kRead = 11,
  kWrite = 12,
  kFileChanged = 13,
  kFunction = 14,
  kInitializer = 15,
  kAccessor = 16,
  kObject = 17
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
// Record-level admission for the value-tree route, not blanket source
// admission for every instruction or method in an admitted profile.
[[nodiscard]] bool SupportsRecordProgramProfile(abc::ProfileId profile);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_PROGRAM_H_
