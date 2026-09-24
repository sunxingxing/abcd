#include "abcd/source/exception_source.h"

#include "semantic_ast_input.h"

namespace abcd::source {
SourceAstResult BuildExceptionSourceAst(
    const ExceptionAstInput& input, analysis::SemanticRegionTree regions,
    analysis::SemanticBudget budget,
    std::span<const std::uint8_t> named_scopes,
    std::optional<analysis::RestPrologue> rest) {
  const auto checked = analysis::VerifyExceptionInputs(
      input.flow, input.ssa, input.environments, input.values, budget.work);
  if (const auto* failure = std::get_if<analysis::ExceptionInputFailure>(&checked))
    return SourceFailure{
        failure->kind == analysis::ExceptionInputError::kResourceLimit
            ? SourceError::kResourceLimit : SourceError::kInvalidEffect,
        failure->pc};
  if (regions.allocated_bytes > budget.bytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  const auto indexed = analysis::InstructionSsa::Build(
      input.flow, input.ssa, budget.bytes - regions.allocated_bytes);
  const auto* ssa = std::get_if<analysis::InstructionSsa>(&indexed);
  if (!ssa)
    return SourceFailure{
        std::get<analysis::InstructionSsaError>(indexed) ==
                analysis::InstructionSsaError::kResourceLimit
            ? SourceError::kResourceLimit : SourceError::kInvalidEffect, {}};
  const AstMethod method{input.flow.instructions, input.blocks, *ssa};
  const AstProof proof{input.flow.instructions.size(), input.values.Facts(),
                       input.locals.plan, input.module_reads, {}};
  return BuildSourceAst(method, proof, std::move(regions),
      budget.bytes - ssa->AllocatedBytes(), named_scopes, rest);
}
}  // namespace abcd::source
