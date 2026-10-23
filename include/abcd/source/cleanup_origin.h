#ifndef ABCD_SOURCE_CLEANUP_ORIGIN_H_
#define ABCD_SOURCE_CLEANUP_ORIGIN_H_

#include "abcd/abc/method_body.h"

namespace abcd::source {
// kMove: a handler `sta vR`/`lda vR` that keeps the pending exception in acc.
enum class CleanupInstructionKind : std::uint8_t { kPop, kRethrow, kMove };
// An original entry instruction implemented by native scope unwinding. It has
// no fabricated generated-text range and is not globally unreachable.
struct EnvironmentCleanupSite {
  abc::CodeOffset pc;
  CleanupInstructionKind kind;
  std::optional<abc::CodeOffset> allocation;
};
}  // namespace abcd::source
#endif
