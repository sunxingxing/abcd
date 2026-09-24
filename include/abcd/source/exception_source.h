#ifndef ABCD_SOURCE_EXCEPTION_SOURCE_H_
#define ABCD_SOURCE_EXCEPTION_SOURCE_H_

#include "abcd/analysis/exception_inputs.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/source/semantic_source.h"

namespace abcd::source {
// All references belong to the same unchanged body and outlive this call.
// locals.loads must have seeded values; every store is rechecked below.
// blocks supplies original instruction layout only, never normal-only SSA.
struct ExceptionAstInput {
  const analysis::ExceptionFlow& flow;
  const analysis::ExceptionSsa& ssa;
  const analysis::EnvironmentFlow& environments;
  const analysis::ExceptionValueFlow& values;
  const analysis::ExceptionLocals& locals;
  const analysis::NormalFlow& blocks;
  // Original-coordinate ProveExceptionModules certificates whose load facts
  // seeded values. Not normal-CFG kGuarded certificates.
  std::span<const analysis::ModuleReadFact> module_reads = {};
};

// regions is the same-body BuildExceptionRegionTree result. Rechecks all
// reachable inputs before invoking the common AST pass. No captured read
// premises or normal-only call fusion are inferred at this boundary.
// Named-scope/rest certificates obey BuildSourceAst's ownership contract.
// Byte admission includes the instruction-coordinate index and AST scratch.
// budget.work bounds input verification; the shared AST retains its existing
// size-bounded algorithm contract, not a new shared work counter.
[[nodiscard]] SourceAstResult BuildExceptionSourceAst(
    const ExceptionAstInput& input, analysis::SemanticRegionTree regions,
    analysis::SemanticBudget budget = {},
    std::span<const std::uint8_t> named_scopes = {},
    std::optional<analysis::RestPrologue> rest = {});
}  // namespace abcd::source
#endif
