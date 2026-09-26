#include "capture_evaluation.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<CapturedFunctionBinding, CaptureEvaluationFailure> BindCaptureEvaluation(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const CaptureCells& cells, FunctionOwnerId parent_owner,
    FunctionOwnerId child_owner, analysis::SemanticBudget budget) {
  using Error = CaptureEvaluationError;
  const auto fail = [](Error error) { return CaptureEvaluationFailure{error}; };
  try {
    if (parent_owner == child_owner) return fail(Error::kOwner);
    const auto checked = VerifyCaptureCells(index, parent, child, site, creation, cells, budget);
    if (const auto* error = std::get_if<CaptureCellError>(&checked))
      return CaptureEvaluationFailure{*error};
    const auto verification_work = std::get<std::uint64_t>(checked);
    if (verification_work > budget.work) return fail(Error::kWorkLimit);
    budget.work -= verification_work;
    // The common expression model currently admits DEFINEFUNC only. Method
    // home-object binding requires its own proof, not a function-shaped cast.
    const auto& operation = parent.values.graph.ops[cells.creation.value].operation;
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
}  // namespace abcd::source
