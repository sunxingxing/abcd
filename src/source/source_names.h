#ifndef ABCD_SOURCE_SOURCE_NAMES_H_
#define ABCD_SOURCE_SOURCE_NAMES_H_
#include "recovery_flow.h"
namespace abcd::source {
struct SourceNameFacts {
  SourceNameIndex index;
  std::vector<SourceBindingShadow> shadows;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
struct SourceNameResults {
  std::vector<SourceNameResolution> resolutions;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Checked spelling identities cross the text boundary once. Current lexical
// regions define activation; neither name resolution nor containment proves TDZ.
[[nodiscard]] std::variant<SourceNameFacts,TreeStructureError> AnalyzeSourceNames(
    const RecoveryFlow&,std::span<const SourceBindingPresence>,
    std::span<const SourceBindingShadow>,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<SourceNameResults,TreeStructureError> ResolveSourceNames(
    const RecoveryFlow&,const SourceNameIndex&,std::span<const SourceNameQuery>,analysis::SemanticBudget = {});
[[nodiscard]] std::variant<SourceNameResults,TreeStructureError> ResolveSourceNames(
    const RecoveryFlow&,std::span<const SourceNameQuery>,analysis::SemanticBudget = {});
}
#endif
