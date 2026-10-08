#ifndef ABCD_SOURCE_SOURCE_NAME_RETIREMENT_H_
#define ABCD_SOURCE_SOURCE_NAME_RETIREMENT_H_
#include "source_names.h"
namespace abcd::source {
enum class SourceRetirement : std::uint8_t {kFixed,kProposed};
struct SourceRetirementQuery {
  SourceNameQuery lookup;
  SymbolId binding;
  // The read exists only when this inert copy destination remains. Absence
  // means the read is observable independently of declaration retirement.
  std::optional<SymbolId> retained_by;
};
struct SourceRetentionDemand {SymbolId binding;std::optional<SymbolId> retained_by;};
struct SourceNameRetirement {
  std::vector<SourceBindingPresence> presence;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Pure proposal analysis. Callers prove value, cell, completion and effect
// preservation before proposing retirement. Symbol lookup roots must be fixed
// retained bindings. This solver owns only name demand and conditional reads;
// it neither deletes definitions nor mutates the recovery plan.
[[nodiscard]] std::variant<SourceNameRetirement,TreeStructureError> AnalyzeSourceNameRetirement(
    const RecoveryFlow&,std::span<const SourceRetirement>,
    std::span<const SourceRetirementQuery>,std::span<const SourceRetentionDemand>,
    analysis::SemanticBudget = {});
}
#endif
