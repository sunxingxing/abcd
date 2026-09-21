#ifndef ABCD_ANALYSIS_EXCEPTION_DISPATCH_H_
#define ABCD_ANALYSIS_EXCEPTION_DISPATCH_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "abcd/abc/method_body.h"

namespace abcd::analysis {

struct TryId {
  std::uint32_t value = 0;
  auto operator<=>(const TryId&) const = default;
};

struct HandlerId {
  std::uint32_t value = 0;
  auto operator<=>(const HandlerId&) const = default;
};

struct DispatchTarget {
  TryId region;
  HandlerId handler;
  abc::CodeOffset handler_pc;
  auto operator<=>(const DispatchTarget&) const = default;
};

struct DispatchSpan {
  abc::CodeOffset begin;
  abc::CodeOffset end;
  DispatchTarget target;
};

struct ExceptionDispatch {
  std::vector<DispatchSpan> spans;
  // Maps a bytecode PC to the pinned dynamic runtime's selected catch. This
  // does not determine whether the instruction at that PC can throw.
  [[nodiscard]] std::optional<DispatchTarget> Find(abc::CodeOffset pc) const;
};

struct DispatchBudget {
  // Additional peak storage owned by construction; the input body remains
  // caller-owned and immutable throughout this call.
  std::size_t bytes = 64 * 1024 * 1024;
};

enum class DispatchError {
  kUnsupportedProfile,
  kEmptyBody,
  kMalformedBody,
  kInvalidBoundary,
  kResourceLimit,
};

struct DispatchFailure {
  DispatchError kind;
  abc::CodeOffset offset;
};

using DispatchResult = std::variant<ExceptionDispatch, DispatchFailure>;

// Borrows an already decoded body without modifying or retaining it.
[[nodiscard]] DispatchResult BuildExceptionDispatch(const abc::MethodBody& body,
                                                    abc::ProfileId profile,
                                                    DispatchBudget budget = {});

}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_EXCEPTION_DISPATCH_H_
