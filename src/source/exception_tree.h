#ifndef ABCD_SOURCE_EXCEPTION_TREE_H_
#define ABCD_SOURCE_EXCEPTION_TREE_H_

#include "exception_control.h"
#include "function_tree.h"

namespace abcd::source {
struct ExceptionTreeInput {
  const abc::MethodBody& body;
  // Copy the small borrowed-view descriptor; its referents remain owner-held.
  analysis::PhasedValueInput input;
  const ExceptionPlan& regions;
  const ExceptionControl& control;
};
// Deliberately distinct from a publication receipt: original phase/control,
// evaluation and independent tree correspondence are checked. Callable ABI,
// same-file constant resolution and family enclosure still need admission.
// No VerifiedFunction or source receipt follows from this draft alone.
struct ExceptionFunctionDraft {
  FunctionTree tree;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<ExceptionFunctionDraft, FunctionTreeError>
BuildExceptionFunctionDraft(const analysis::ValueProgram& graph,
    const ExceptionTreeInput& exceptions, const EvaluationPlan& evaluation,
    FunctionSyntax syntax, ExpressionConstants constants,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
