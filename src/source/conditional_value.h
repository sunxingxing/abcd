#ifndef ABCD_SOURCE_CONDITIONAL_VALUE_H_
#define ABCD_SOURCE_CONDITIONAL_VALUE_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
struct ConditionalValue {
  analysis::ScalarSlot target;
  analysis::InstructionId body;
  analysis::InstructionId alternate;
  std::optional<bool> boolean_body;
};
// RegionIf already proves single-entry arms. One admitted expression commit
// per arm to the same private slot can become a conditional value.
std::optional<ConditionalValue> ProveConditionalValue(
    const SourceAst& ast, const analysis::NormalFlow& flow,
    const analysis::RegionIf& region);
}  // namespace abcd::source
#endif
