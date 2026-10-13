#include "tree_semantics.h"
#include "original_flow.h"

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
    const ExceptionTreeInput* exceptions,
    TreeControlCheck control_check, TreeSemanticFailure* failure) {
  if (failure) *failure = {};
  using Error = TreeSemanticError;
  try {
    auto checked_flow = exceptions
        ? VerifyExceptionTreeFlow(graph, *exceptions, evaluation, tree, budget, control_check, failure ? &failure->location : nullptr)
        : VerifyTreeFlow(graph, control, evaluation, tree, budget, failure ? &failure->location : nullptr);
    auto* flow = std::get_if<TreeFlow>(&checked_flow);
    if (!flow) {
      const auto error = std::get<TreeFlowError>(checked_flow);
      if (failure) failure->cause = error;
      if (error == TreeFlowError::kResourceLimit) return Error::kResourceLimit;
      if (error == TreeFlowError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    TreeCheckBudget checked(budget);
    checked.Admit(flow->allocated_bytes, 1);
    checked.Step(flow->work);
    auto original=RetainOriginalFlow(graph,tree,*flow,checked);
    if (evaluation.cells) {
      original->environments=evaluation.cells->environments;
      RequireTree(original->environments!=nullptr,TreeStructureError::kOwnership);
      // The source result can outlive EvaluationPlan. Admit retained topology
      // once here; current/trial flow borrows the same immutable owner.
      checked.Admit(original->environments->allocated_bytes,1);
    }
    CheckTreeOperands(graph, evaluation, tree, *flow, constants, checked,*original);
    CheckTreeCorrespondenceOrigins(graph, evaluation, tree, *flow, checked);
    IndexOriginalInputs(*original,checked);
    flow->original=std::move(original);
    flow->allocated_bytes=checked.Bytes();
    return TreeSemantics{graph.revision, graph.cfg_revision, checked.Bytes(), checked.Work(), std::move(*flow)};
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
    ExpressionConstants constants, analysis::SemanticBudget budget, TreeSemanticFailure* failure) {
  auto result = VerifySemantics(graph, control, evaluation, tree, constants, budget, nullptr, TreeControlCheck::kFresh, failure);
  if (failure) if (const auto* error = std::get_if<TreeSemanticError>(&result)) failure->kind = *error;
  return result;
}
std::variant<TreeSemantics, TreeSemanticError> VerifyExceptionTreeSemantics(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget, TreeControlCheck control_check, TreeSemanticFailure* failure) {
  auto result = VerifySemantics(graph, exceptions.control.skeleton, evaluation, tree, constants, budget, &exceptions,
      control_check, failure);
  if (failure) if (const auto* error = std::get_if<TreeSemanticError>(&result)) failure->kind = *error;
  return result;
}
}  // namespace abcd::source
