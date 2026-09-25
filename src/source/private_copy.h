#ifndef ABCD_SOURCE_PRIVATE_COPY_H_
#define ABCD_SOURCE_PRIVATE_COPY_H_

#include "semantic_ast_input.h"

namespace abcd::source {
bool IsPureCopyStep(const analysis::SemanticInstruction& instruction);
bool IsRetargetedCopyAnchor(const SourceStatement& statement,
                           const SourceStatement& producer);
// Resolve the saved SSA identity through an adjacent origin chain and certified
// guards. end is exclusive and must not cross the caller's block/consumer.
std::optional<analysis::SsaValueId> RetargetedOutput(
    const AstMethod& method, const SourceAst& ast, std::size_t producer,
    std::size_t end, abc::RegisterId target, std::span<const std::size_t> uses);
// Resolve direct or retargeted pure register commits. Callers separately
// prove the resulting definition's sole consumer and unchanged private inputs.
std::optional<analysis::SsaValueId> PrivateCopyOutput(
    const AstMethod& method, const SourceAst& ast, std::size_t producer,
    std::size_t end, abc::RegisterId target, std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
