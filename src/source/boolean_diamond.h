#ifndef ABCD_SOURCE_BOOLEAN_DIAMOND_H_
#define ABCD_SOURCE_BOOLEAN_DIAMOND_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct BooleanDiamond {
  analysis::ScalarSlot target;
  bool body_value;
};
// RegionIf already proves single-entry arms. Only opposite, effect-free
// boolean commits to the same private slot can replace this whole region.
std::optional<BooleanDiamond> ProveBooleanDiamond(
    const SourceAst& ast, const analysis::NormalFlow& flow,
    const analysis::RegionIf& region);
}  // namespace abcd::source
#endif
