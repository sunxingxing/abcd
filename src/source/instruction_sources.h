#ifndef ABCD_SOURCE_INSTRUCTION_SOURCES_H_
#define ABCD_SOURCE_INSTRUCTION_SOURCES_H_

#include "function_input.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
class PhasedBody;
struct ElidedInstruction {};
using InstructionTarget = std::variant<ExprId, StmtId, AnchorId, PropertyId, ParameterId, ElidedInstruction>;
struct InstructionSource {
  TreeOriginId origin;
  InstructionTarget target;
};
struct InstructionSources {
  // Original input-site order, not a PC-sized array or generated-text index.
  std::vector<InstructionSource> sites;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class InstructionSourceError : std::uint8_t {
  kIdentity, kRevision, kOrigin, kPlacement, kCoverage, kResourceLimit, kWorkLimit
};
// Requires fresh semantic/default/flow proofs for the unchanged residual tree.
// Accounts every original instruction once; it does not prove optimizations or
// authorize erasure itself. Stable one-origin-per-site work IR is required.
// O(input sites + work ops + placements), with bounded dense ordinal indexes.
[[nodiscard]] std::variant<InstructionSources, InstructionSourceError> BuildInstructionSources(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const FunctionTree& tree, analysis::SemanticBudget budget = {});
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
