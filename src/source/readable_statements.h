#ifndef ABCD_SOURCE_READABLE_STATEMENTS_H_
#define ABCD_SOURCE_READABLE_STATEMENTS_H_
#include "binding_initializer.h"
#include "loop_syntax.h"
#include "nullish_syntax.h"
namespace abcd::source {
enum class ReadableGuard : std::uint8_t {
  kNone, kNoCertificate, kBinding, kRhsEffect, kProjection, kRemove
};
enum class ReadableBranch : std::uint8_t { kKeep, kDropElse, kInvert };
struct ReadableStatements {
  std::vector<ReadableGuard> guards;
  std::vector<ReadableBranch> branches;
  std::vector<std::uint8_t> plain_breaks, omit_labels, omit_snapshots;
  std::vector<std::optional<ExprId>> switch_values;
  bool operator==(const ReadableStatements&) const = default;
};
// Printer-only equivalences over the final, independently verified projections.
// No recovery/ownership permission is granted to a tree by this certificate.
[[nodiscard]] ReadableStatements BuildReadableStatements(const FunctionTree&,
    const BindingInitializers&, const NullishSyntax&, const LoopSyntax&,
    const OriginalFlow*, TreeCheckBudget&);
[[nodiscard]] bool VerifyReadableStatements(const FunctionTree&,
    const BindingInitializers&, const NullishSyntax&, const LoopSyntax&,
    const OriginalFlow*, const ReadableStatements&, TreeCheckBudget&);
}
#endif
