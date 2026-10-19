#ifndef ABCD_SOURCE_CREATOR_CLASS_OCCURRENCE_H_
#define ABCD_SOURCE_CREATOR_CLASS_OCCURRENCE_H_
#include "abcd/source/family_creation_failure.h"
namespace abcd::source {
// Coordinates in the caller's original occurrence family, not method entities.
struct CreatorClassOccurrence {
  abc::EntityOffset family_root{};
  FunctionOwnerId first_owner{};
  FamilyOccurrenceId member{};
  bool operator==(const CreatorClassOccurrence&) const = default;
};
}  // namespace abcd::source
#endif
