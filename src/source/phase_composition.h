#ifndef ABCD_SOURCE_PHASE_COMPOSITION_H_
#define ABCD_SOURCE_PHASE_COMPOSITION_H_

#include "evaluation_plan.h"
#include "tree_checks.h"

namespace abcd::source {
// Phased expression composition across committed selections
// (docs/design/source-recovery/call-references.md §5.4). A committed top-level
// selection is one atomic event of its consumer's tree; its predicate, prefix
// and arms stay proved by the selection verifier. The shared proof expands the
// final-ownership tree of one execution root in source post-order and matches
// it against the actual normal path ending at that root: single-entry,
// copy-free edges, each selection crossed from its header to its own join.
// Every moved throw point keeps the root's handler and has no catch copies.
//
// Requires committed phase selections. Only adds root->inline edges for trees
// containing a selection; a tree is committed whole or not at all, and the
// whole composition is withdrawn unless the selection proof is unchanged
// except for its execution owner.
void ComposePhaseExpressions(const analysis::ValueProgram& graph, EvaluationPlan& plan,
    TreeCheckBudget& budget);
// Repair only unmet member references, treating a verified capture load/guard
// as an atomic throwing operand. Already valid call plans keep every placement.
// It uses the same path/ownership proof without requiring a selection region.
void ComposeCheckedCalls(const analysis::ValueProgram&, EvaluationPlan&, TreeCheckBudget&);
// Independently re-proves every listed composition. Returns an OpId-indexed
// mask of operations owned by a composed tree (roots and inline members); the
// ordinary corridor equations do not apply to them. Throws EvaluationError.
[[nodiscard]] std::vector<std::uint8_t> VerifyPhaseCompositions(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, TreeCheckBudget& budget);
}  // namespace abcd::source
#endif
