#include "edge_copy_index.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<std::uint64_t, CopyError> VerifyEdgeCopies(
    std::span<const ParallelCopy> copies, const EdgeCopies& plan, analysis::SemanticBudget allowance) {
  try {
    CopyBudget budget(allowance);
    CopyIndex index(copies, budget);
    RequireCopy(plan.temporaries <= copies.size() &&
        (plan.steps.size() <= copies.size() || plan.steps.size() - copies.size() <= plan.temporaries), CopyError::kTemporary);
    std::vector<analysis::ValueOperand> locations;
    std::vector<std::optional<analysis::ValueOperand>> temporaries;
    std::vector<std::uint8_t> written, read;
    budget.Reserve(locations, copies.size());
    budget.Array(temporaries, plan.temporaries);
    budget.Array(written, copies.size());
    budget.Array(read, plan.temporaries);
    for (const auto& copy : copies) { budget.Step(); locations.emplace_back(copy.destination); }
    for (const auto& step : plan.steps) {
      budget.Step();
      analysis::ValueOperand token;
      if (const auto* temporary = std::get_if<CopyTempId>(&step.source)) {
        RequireCopy(temporary->value < temporaries.size() && temporaries[temporary->value].has_value(), CopyError::kTemporary);
        token = *temporaries[temporary->value]; read[temporary->value] = 1;
      } else {
        token = std::get<analysis::ValueOperand>(step.source);
        if (const auto* value = std::get_if<analysis::ValueId>(&token))
          if (const auto row = index.Find(*value)) token = locations[*row];
      }
      if (const auto* temporary = std::get_if<CopyTempId>(&step.destination)) {
        RequireCopy(temporary->value < temporaries.size() && !temporaries[temporary->value], CopyError::kTemporary);
        temporaries[temporary->value] = token;
      } else {
        const auto row = index.Find(std::get<analysis::ValueId>(step.destination));
        RequireCopy(row && !written[*row], CopyError::kDestination);
        locations[*row] = token; written[*row] = 1;
      }
    }
    for (std::size_t i = 0; i < copies.size(); ++i) {
      budget.Step(); RequireCopy(SameCopyValue(locations[i], copies[i].source), CopyError::kValue);
    }
    for (std::size_t i = 0; i < temporaries.size(); ++i) {
      budget.Step(); RequireCopy(temporaries[i].has_value() && read[i], CopyError::kTemporary);
    }
    return budget.Work();
  } catch (CopyError error) { return error;
  } catch (const std::bad_alloc&) { return CopyError::kResourceLimit;
  } catch (const std::length_error&) { return CopyError::kResourceLimit; }
}
}  // namespace abcd::source
