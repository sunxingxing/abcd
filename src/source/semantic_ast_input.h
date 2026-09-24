#ifndef ABCD_SOURCE_SEMANTIC_AST_INPUT_H_
#define ABCD_SOURCE_SEMANTIC_AST_INPUT_H_

#include "abcd/analysis/instruction_ssa.h"
#include "abcd/analysis/exception_calls.h"
#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Borrowed, owner-verified instruction coordinates for the common AST pass.
// Exception owners must project binding facts, not rebuild a normal-only SSA.
struct AstMethod {
  std::span<const analysis::SemanticInstruction> instructions;
  const analysis::NormalFlow& flow;
  const analysis::InstructionSsa& ssa;
};
struct AstProof {
  std::size_t instructions;
  std::span<const analysis::ValueKinds> values;
  const analysis::LocalAccessPlan& locals;
  std::span<const analysis::ModuleReadFact> module_reads;
  std::span<const analysis::CapturedReadFact> captured_reads;
  std::span<const std::optional<analysis::ExceptionCallRoute>> call_routes = {};
  std::optional<analysis::ValueKinds> At(analysis::SsaValueId id) const {
    if (id.value >= values.size()) return {};
    return values[id.value];
  }
};
SourceAstResult BuildSourceAst(const AstMethod& method, const AstProof& proof,
    analysis::SemanticRegionTree regions, std::size_t budget_bytes,
    std::span<const std::uint8_t> named_scopes,
    std::optional<analysis::RestPrologue> rest);
}  // namespace abcd::source
#endif
