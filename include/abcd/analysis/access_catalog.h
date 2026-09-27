#ifndef ABCD_ANALYSIS_ACCESS_CATALOG_H_
#define ABCD_ANALYSIS_ACCESS_CATALOG_H_

#include <span>

#include "abcd/abc/instruction.h"

namespace abcd::analysis {
enum class AccessFamily : std::uint8_t {
  kScalar, kConstant, kEnvironment, kLexical, kModule, kGlobal,
  kProperty, kOwnDefinition, kReceiver, kSuper, kPrivate,
  kSendable, kLazyModule, kPatch, kNamespace, kIterator,
  kArguments, kTemplate, kCoroutine, kSpread, kPrototype
};
struct AccessSpec {
  abc::Opcode opcode;
  AccessFamily family;
  std::uint8_t size;
  bool reads_acc;
  bool writes_acc;
};
// Encoding inventory from the pinned v7 ISA, NOT profile-specific semantic
// support or a source-publication certificate. A family does not equate its
// members (e.g. indexed/property-key reads or own define/ordinary set).
// Static immutable storage, no allocation; lookup O(log opcode count).
[[nodiscard]] std::span<const AccessSpec> PinnedAccessCatalog();
[[nodiscard]] const AccessSpec* FindPinnedAccess(abc::Opcode opcode);
}  // namespace abcd::analysis
#endif
