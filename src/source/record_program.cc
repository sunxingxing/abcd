#include "abcd/source/record_program.h"

#include "abcd/abc/profile_capabilities.h"

namespace abcd::source {
bool SupportsRecordProgramProfile(abc::ProfileId profile) {
  return abc::HasPinnedRuntimeSemantics(profile);
}
}  // namespace abcd::source
