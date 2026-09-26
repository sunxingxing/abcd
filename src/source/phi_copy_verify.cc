#include "phi_copy_checks.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<std::uint64_t, PhiCopyError> VerifyPhiCopies(
    const analysis::ValueProgram& graph, const PhiCopies& plan, analysis::SemanticBudget allowance) {
  try {
    RequirePhiCopy(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, PhiCopyError::kRevision);
    CopyBudget budget(allowance);
    CheckPhiFlow(graph, budget);
    RequirePhiCopy(plan.inputs.size() == plan.copies.size(), PhiCopyError::kInput);
    std::vector<std::uint8_t> seen;
    budget.Array(seen, graph.uses.size());
    std::size_t first = 0;
    std::optional<analysis::IncomingKey> previous;
    for (const auto& bundle : plan.bundles) {
      budget.Step();
      RequirePhiCopy(bundle.first == first && bundle.count != 0 &&
          bundle.count <= plan.copies.size() - first &&
          (!previous || IncomingLess(*previous, bundle.incoming)), PhiCopyError::kBundle);
      for (std::size_t i = first; i < first + bundle.count; ++i) {
        budget.Step();
        const auto input = plan.inputs[i];
        RequirePhiCopy(input.value < graph.uses.size() && !seen[input.value], PhiCopyError::kInput);
        seen[input.value] = 1;
        const auto& use = graph.uses[input.value];
        RequirePhiCopy(PhiInput(use) && use.incoming && SameIncoming(*use.incoming, bundle.incoming), PhiCopyError::kInput);
        CheckPhiDomain(graph, use);
        RequirePhiCopy(std::get<analysis::ValueId>(use.owner) == plan.copies[i].destination &&
            SameCopyValue(use.operand, plan.copies[i].source), PhiCopyError::kInput);
      }
      const auto checked = VerifyEdgeCopies(std::span<const ParallelCopy>{plan.copies}.subspan(first, bundle.count),
                                            bundle.schedule, budget.Remaining());
      if (const auto* error = std::get_if<CopyError>(&checked)) throw PhiScheduleError(*error);
      budget.Step(std::get<std::uint64_t>(checked));
      first += bundle.count;
      previous = bundle.incoming;
    }
    RequirePhiCopy(first == plan.copies.size(), PhiCopyError::kBundle);
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step(); RequirePhiCopy(bool(seen[i]) == PhiInput(graph.uses[i]), PhiCopyError::kInput);
    }
    return budget.Work();
  } catch (PhiCopyError error) { return error;
  } catch (CopyError error) { return PhiScheduleError(error);
  } catch (const std::bad_alloc&) { return PhiCopyError::kResourceLimit;
  } catch (const std::length_error&) { return PhiCopyError::kResourceLimit; }
}
}  // namespace abcd::source
