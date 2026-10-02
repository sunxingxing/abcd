#ifndef ABCD_SOURCE_INSTRUCTION_SOURCES_H_
#define ABCD_SOURCE_INSTRUCTION_SOURCES_H_

#include "function_input.h"
#include "cleanup_body.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
class PhasedBody;
struct ElidedInstruction {};
struct UnreachableInstruction {};
struct CleanupInstruction { CleanupInstructionKind kind; };
using InstructionTarget = std::variant<ExprId, StmtId, AnchorId, PropertyId, ParameterId, ElidedInstruction, UnreachableInstruction, CleanupInstruction>;
struct InstructionSource {
  analysis::InputSiteId site;
  TreeOriginId origin;
  InstructionTarget target;
};
struct InstructionSources {
  // One entry per work operation, in operation order. Tail duplication keeps
  // every occurrence of the original site, including elisions and anchors.
  std::vector<InstructionSource> occurrences;
  std::size_t input_sites = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class InstructionSourceError : std::uint8_t {
  kIdentity, kRevision, kOrigin, kPlacement, kCoverage, kResourceLimit, kWorkLimit
};
// Requires fresh semantic/default/flow proofs for the unchanged residual tree.
// Optional cleanup is the same-body sealed normal projection used to build
// input; cleanup sites get an explicit non-syntax reason, never a purity claim.
// Covers every input site and every work operation with its own origin/target.
// Multiple occurrences do not prove duplication or authorize erasure: callers
// must retain the existing transformation and fresh semantic/flow proofs.
// O(input sites + work ops + placements), with bounded dense ordinal indexes.
[[nodiscard]] std::variant<InstructionSources, InstructionSourceError> BuildInstructionSources(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const FunctionTree& tree, analysis::SemanticBudget budget = {},
    const CleanupBody* cleanup = nullptr);
// Same census over original phased instructions, not Before/Commit/Catch node
// ordinals. Requires fresh exception tree/control/flow proofs. Projected pure
// elisions and aliases retain their original boundary anchors; they are not
// erased IR operations. Phase signature migration is not admitted. This does
// not create a source publication receipt.
[[nodiscard]] std::variant<InstructionSources, InstructionSourceError> BuildInstructionSources(
    const PhasedBody& input, const abc::MethodBody& body,
    const FunctionTree& tree, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
