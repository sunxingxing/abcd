#include "tree_semantics.h"

#include <new>
#include <stdexcept>

#include "tree_correspondence.h"
#include "exception_tree.h"

namespace abcd::source {
namespace {
std::variant<TreeSemantics, TreeSemanticError> VerifySemantics(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget,
    const ExceptionTreeInput* exceptions) {
  using Error = TreeSemanticError;
  try {
    auto checked_flow = exceptions
        ? VerifyExceptionTreeFlow(graph, *exceptions, evaluation, tree, budget)
        : VerifyTreeFlow(graph, control, evaluation, tree, budget);
    const auto* flow = std::get_if<TreeFlow>(&checked_flow);
    if (!flow) {
      const auto error = std::get<TreeFlowError>(checked_flow);
      if (error == TreeFlowError::kResourceLimit) return Error::kResourceLimit;
      if (error == TreeFlowError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    TreeCheckBudget checked(budget);
    checked.Admit(flow->allocated_bytes, 1);
    checked.Step(flow->work);
    CheckTreeOperands(graph, evaluation, tree, *flow, constants, checked);
    CheckTreeCorrespondenceOrigins(graph, tree, *flow, checked);
    return TreeSemantics{graph.revision, graph.cfg_revision, checked.Bytes(), checked.Work()};
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<TreeSemantics, TreeSemanticError> VerifyTreeSemantics(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget) {
  return VerifySemantics(graph, control, evaluation, tree, constants, budget, nullptr);
}
std::variant<TreeSemantics, TreeSemanticError> VerifyExceptionTreeSemantics(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget) {
  return VerifySemantics(graph, exceptions.control.skeleton, evaluation, tree, constants, budget, &exceptions);
}
}  // namespace abcd::source
