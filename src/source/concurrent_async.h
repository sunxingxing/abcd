#ifndef ABCD_SOURCE_CONCURRENT_ASYNC_H_
#define ABCD_SOURCE_CONCURRENT_ASYNC_H_

#include "abcd/source/async_completion.h"
#include "abcd/source/concurrent_failure.h"

namespace abcd::source {
struct AsyncNotificationPort {
  analysis::InstructionId settlement, notification, returned;
  analysis::AsyncSettlement kind;
  bool operator==(const AsyncNotificationPort&) const = default;
};
// Raw completion ports, not normalized resolution-value returns. The pinned
// interpreter commits the promise to acc only on successful settlement.
struct AsyncNotifications {
  std::vector<AsyncNotificationPort> ports;
  std::vector<std::optional<std::uint32_t>> members;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<AsyncNotifications, ConcurrentFailure> ProveAsyncNotifications(
    const abc::MethodBody&, const abc::FrameLayout&, abc::ProfileId,
    analysis::SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t, ConcurrentFailure> VerifyAsyncNotifications(
    const abc::MethodBody&, const abc::FrameLayout&, abc::ProfileId,
    const AsyncNotifications&, analysis::SemanticBudget = {});
struct ConcurrentAsyncBody {
  abc::MethodBody body;
  std::vector<abc::CodeOffset> origins;
  std::vector<analysis::AwaitSite> awaits;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// First prove the raw promise/notification/return ports, then remove only their
// closed, nonthrowing suffixes. The existing async owner still proves private
// state, resume, implicit rejection, user handlers and parameter prelude.
using ConcurrentAsyncResult = std::variant<ConcurrentAsyncBody, ConcurrentFailure, AsyncCompletionFailure>;
[[nodiscard]] ConcurrentAsyncResult NormalizeConcurrentAsync(
    const abc::MethodBody&, const abc::FrameLayout&, abc::ProfileId,
    analysis::SemanticBudget = {});
struct ConcurrentAsyncEvidence {
  abc::MethodBody original;
  abc::FrameLayout frame;
  abc::ProfileId profile;
  std::size_t allocated_bytes = 0;
};
// Replay from immutable raw input and match every working/original origin and
// native success return. The implicit rejection port is re-proved by the async
// owner, not fabricated as a ReturnStmt or a resolution-value expression.
[[nodiscard]] std::variant<std::uint64_t, ConcurrentFailure> VerifyConcurrentAsync(
    const ConcurrentAsyncEvidence&, const analysis::ValueProgram&,
    analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
