#ifndef ABCD_SOURCE_EXPRESSION_ORIGINS_H_
#define ABCD_SOURCE_EXPRESSION_ORIGINS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Changing a result destination must leave evaluation and origin ownership
// on this statement. Only proved member-call fusions carry their origins
// through generic assignment/return wrappers; staged calls are separate nodes.
inline bool CanRetargetExpression(const SourceStatement& statement,
                                 const SourceExpression& expression) {
  if (statement.class_origin || statement.previous_origin) return false;
  if (!statement.fused_read && !statement.fused_spill && !statement.fused_reload)
    return true;
  if (!statement.fused_read || !statement.read_distance ||
      !std::holds_alternative<MemberCallExpression>(expression) ||
      statement.fused_spill.has_value() != statement.fused_reload.has_value()) return false;
  return !statement.fused_spill || (statement.spill_distance && statement.reload_distance);
}
}  // namespace abcd::source
#endif
