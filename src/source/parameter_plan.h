#ifndef ABCD_SOURCE_PARAMETER_PLAN_H_
#define ABCD_SOURCE_PARAMETER_PLAN_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
// An ABI ordinal identifies a formal; its entry identifies the immutable SSA
// invocation value, not the mutable source parameter after a later write.
// Absent entries preserve unused formals without inventing undefined values.
struct ParameterEntryValue {
  std::optional<analysis::ValueId> entry;
};
struct ParameterPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ParameterEntryValue> formals;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ParameterPlanError : std::uint8_t {
  kOrdinal, kDuplicateEntry, kResourceLimit, kWorkLimit
};
// O(V+P) time/O(P) storage. Shared by all callable kinds: no constructor-only
// names, register heuristics, default-prologue motion or source-text parsing.
// Call after import/rewrite verification. A later write or phi is a distinct
// ValueId and must never silently replace this entry when binding old uses.
[[nodiscard]] std::variant<ParameterPlan, ParameterPlanError> BuildParameterPlan(
    const analysis::ValueProgram& graph, std::uint32_t parameter_count,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
