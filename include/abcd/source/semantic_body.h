#ifndef ABCD_SOURCE_SEMANTIC_BODY_H_
#define ABCD_SOURCE_SEMANTIC_BODY_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
using SemanticBodyResult = std::variant<
    PrintedSource, analysis::SemanticFailure, analysis::SemanticProofFailure,
    analysis::RegionFailure, SourceFailure>;
using SemanticTemplateResult = std::variant<
    PrintedTemplate, analysis::SemanticFailure, analysis::SemanticProofFailure,
    analysis::RegionFailure, SourceFailure>;

// The callable owner proves its ABI and private bindings before entry. All
// stages operate on this exact body; no text is published before admission.
// Raw bodies with exception tables use the shared phased pipeline. Pending
// payloads use the explicit versioned kSourceContract environment premise;
// explicit THROW still uses its checked SSA value. Normalized async bodies
// need separate handler ownership; captured cells require the template contract.
// default_length opts an owning non-rest callable into literal-default
// recovery. A successful result's parameter_defaults MUST be emitted by that
// owner in its formal list; its body alone is not an equivalent replacement.
[[nodiscard]] SemanticBodyResult BuildSemanticBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes,
    std::optional<std::uint16_t> default_length = {});
// Parametric child only. The caller must replay every typed capture into the
// parent binding proof; text alone is not a publishable function body.
[[nodiscard]] SemanticTemplateResult BuildSemanticTemplate(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes,
    std::optional<std::uint16_t> default_length = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_BODY_H_
