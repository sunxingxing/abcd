#ifndef ABCD_SOURCE_RECORD_VALUE_H_
#define ABCD_SOURCE_RECORD_VALUE_H_

#include <cstddef>

namespace abcd::source {
struct RecordValue {
  enum class Kind {
    kUnknown,
    kUndefined,
    kHole,
    kClass,
    kFunction,
    kCreatedMethod,
    kString,
    kFalse,
    kTrue,
    kNull,
    kAtom,
    kLiteral,
    kImportSnapshot,
    kLocalSnapshot,
    kGlobalSnapshot,
    kCallResult,
    kChoice,
    kPrototype,
    kInstance,
    kEnvironment
  } kind;
  std::size_t ordinal = 0;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_RECORD_VALUE_H_
