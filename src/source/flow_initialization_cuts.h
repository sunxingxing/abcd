#ifndef ABCD_SOURCE_FLOW_INITIALIZATION_CUTS_H_
#define ABCD_SOURCE_FLOW_INITIALIZATION_CUTS_H_
#include "flow_dominance.h"
namespace abcd::source {
struct FlowInitializationCertificates {
  std::vector<std::uint8_t> clear;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// A sufficient gen/kill certificate on the SAME immutable completion/unit
// graph. Absence requests full must solving, never rejects source recovery.
// No graph per variable, scope-ancestor walk per edge, or plan mutation.
[[nodiscard]] std::variant<FlowInitializationCertificates,TreeStructureError> FindFlowInitializationCertificates(
    const RecoveryFlow&,const FlowDominance&,std::span<const std::optional<FlowPoint>>,
    analysis::SemanticBudget = {});
}
#endif
