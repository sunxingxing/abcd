#include "capture_evaluation.h"
#include "phased_capture_evaluation.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
std::variant<CapturedFunctionBinding, CaptureEvaluationFailure> BindVerifiedCaptures(
    const analysis::ValueProgram& graph, const CaptureCells& cells,
    FunctionOwnerId child_owner, std::uint64_t verification_work, analysis::SemanticBudget budget) {
  using Error = CaptureEvaluationError;
  const auto fail = [](Error error) { return CaptureEvaluationFailure{error}; };
  try {
    if (verification_work > budget.work) return fail(Error::kWorkLimit);
    budget.work -= verification_work;
    // The common expression model currently admits DEFINEFUNC only. Method
    // home-object binding requires its own proof, not a function-shaped cast.
    const auto& operation = graph.ops[cells.creation.value].operation;
    if (!DescribeEvaluation(operation)) return fail(Error::kCreation);
    if (cells.targets.size() > budget.work) return fail(Error::kWorkLimit);
    if (cells.targets.size() > budget.bytes / sizeof(CellId)) return fail(Error::kResourceLimit);
    CapturedFunctionBinding result{{cells.creation, child_owner}};
    result.binding.captures.reserve(cells.targets.size());
    if (result.binding.captures.capacity() > budget.bytes / sizeof(CellId))
      return fail(Error::kResourceLimit);
    for (const auto& target : cells.targets) {
      const auto* cell = std::get_if<ParentCell>(&target.target);
      if (!cell) return fail(Error::kOuterCapture);
      result.binding.captures.push_back(cell->cell);
    }
    result.allocated_bytes = result.binding.captures.capacity() * sizeof(CellId);
    result.work = verification_work + cells.targets.size();
    return result;
  } catch (const std::bad_alloc&) { return fail(Error::kResourceLimit);
  } catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
}  // namespace
std::variant<CapturedFunctionBinding, CaptureEvaluationFailure> BindCaptureEvaluation(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const CaptureCells& cells, FunctionOwnerId parent_owner,
    FunctionOwnerId child_owner, analysis::SemanticBudget budget) {
  if (parent_owner == child_owner) return CaptureEvaluationFailure{CaptureEvaluationError::kOwner};
  const auto checked = VerifyCaptureCells(index, parent, child, site, creation, cells, budget);
  if (const auto* error = std::get_if<CaptureCellError>(&checked)) return CaptureEvaluationFailure{*error};
  return BindVerifiedCaptures(parent.values.graph, cells, child_owner, std::get<std::uint64_t>(checked), budget);
}
std::variant<CapturedFunctionBinding, CaptureEvaluationFailure> BindPhasedCaptureEvaluation(
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const PhasedBody& parent, const abc::MethodBody& body,
    const analysis::ExceptionLocals& locals, const PhasedCreation& creation,
    CaptureFunction child, const FunctionContext& owner, const CaptureCells& cells,
    FunctionOwnerId parent_owner, FunctionOwnerId child_owner, analysis::SemanticBudget budget) {
  if (parent_owner == child_owner) return CaptureEvaluationFailure{CaptureEvaluationError::kOwner};
  const auto checked = VerifyPhasedCaptureCells(index, lookup, parent, body, locals, creation, child, owner, cells, budget);
  if (const auto* error = std::get_if<CaptureCellError>(&checked)) return CaptureEvaluationFailure{*error};
  return BindVerifiedCaptures(parent.Graph(), cells, child_owner, std::get<std::uint64_t>(checked), budget);
}
}  // namespace abcd::source
