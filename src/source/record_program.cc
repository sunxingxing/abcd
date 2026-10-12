#include "abcd/source/record_program.h"

namespace abcd::source {
bool SupportsRecordProgramProfile(abc::ProfileId profile) {
  switch (profile) {
    case abc::ProfileId::kDynamic1202:
    case abc::ProfileId::kDynamic1206:
    case abc::ProfileId::kDynamic1301:
      return true;
    case abc::ProfileId::kDynamic9:
    case abc::ProfileId::kDynamic1102:
    case abc::ProfileId::kDynamic2400:
    case abc::ProfileId::kStatic0107:
    case abc::ProfileId::kUnknown:
      return false;
  }
  return false;
}
}  // namespace abcd::source
