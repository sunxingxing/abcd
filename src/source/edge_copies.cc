#include "edge_copy_index.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
struct CopyDependency {
  std::optional<std::uint32_t> source;
  std::optional<std::uint32_t> first_user;
  std::optional<std::uint32_t> next_user;
  std::optional<CopyTempId> saved;
  std::uint32_t users = 0;
  bool pending = true;
};
}  // namespace
std::variant<EdgeCopies, CopyError> BuildEdgeCopies(
    std::span<const ParallelCopy> copies, analysis::SemanticBudget allowance) {
  try {
    CopyBudget budget(allowance);
    CopyIndex index(copies, budget);
    std::vector<CopyDependency> dependencies;
    std::vector<std::uint32_t> ready;
    budget.Array(dependencies, copies.size());
    budget.Reserve(ready, copies.size());
    EdgeCopies result;
    budget.Admit(1, sizeof(result));
    constexpr std::size_t kMaximumStepsPerCopy = 2; // One write plus at most one cycle save.
    RequireCopy(copies.size() <= std::numeric_limits<std::size_t>::max() / kMaximumStepsPerCopy, CopyError::kResourceLimit);
    budget.Reserve(result.steps, copies.size() * kMaximumStepsPerCopy);
    std::size_t pending = 0;
    for (std::size_t i = 0; i < copies.size(); ++i) {
      budget.Step();
      auto& row = dependencies[i];
      const auto* value = std::get_if<analysis::ValueId>(&copies[i].source);
      if (value && *value == copies[i].destination) { row.pending = false; continue; }
      ++pending;
      if (!value) continue;
      row.source = index.Find(*value);
      if (!row.source) continue;
      auto& source = dependencies[*row.source];
      row.next_user = source.first_user;
      source.first_user = static_cast<std::uint32_t>(i);
      ++source.users;
    }
    for (std::size_t i = 0; i < copies.size(); ++i) {
      budget.Step();
      if (dependencies[i].pending && dependencies[i].users == 0) ready.push_back(static_cast<std::uint32_t>(i));
    }
    std::size_t cursor = 0, cycle = 0;
    while (pending != 0) {
      budget.Step();
      if (cursor == ready.size()) {
        // All remaining components contain cycles. The monotonically advancing
        // cursor avoids rescanning finished components once per cycle.
        while (cycle < copies.size() && !dependencies[cycle].pending) { budget.Step(); ++cycle; }
        RequireCopy(cycle < copies.size(), CopyError::kValue);
        auto& saved = dependencies[cycle];
        const CopyTempId temporary{result.temporaries++};
        result.steps.push_back({temporary, analysis::ValueOperand{copies[cycle].destination}});
        auto user = saved.first_user;
        while (user) {
          budget.Step();
          auto& row = dependencies[*user];
          if (row.pending) { row.saved = temporary; row.source.reset(); }
          user = row.next_user;
        }
        saved.users = 0;
        ready.push_back(static_cast<std::uint32_t>(cycle));
      }
      const auto selected = ready[cursor++];
      auto& row = dependencies[selected];
      RequireCopy(row.pending && row.users == 0, CopyError::kValue);
      result.steps.push_back({copies[selected].destination,
          row.saved ? CopySource{*row.saved} : CopySource{copies[selected].source}});
      row.pending = false;
      --pending;
      if (row.source) {
        auto& source = dependencies[*row.source];
        RequireCopy(source.users != 0, CopyError::kValue);
        --source.users;
        if (source.pending && source.users == 0) ready.push_back(*row.source);
      }
    }
    const auto checked = VerifyEdgeCopies(copies, result, budget.Remaining());
    if (const auto* error = std::get_if<CopyError>(&checked)) return *error;
    budget.Step(std::get<std::uint64_t>(checked));
    result.allocated_bytes = sizeof(result) + result.steps.capacity() * sizeof(CopyStep);
    result.work = budget.Work();
    return result;
  } catch (CopyError error) { return error;
  } catch (const std::bad_alloc&) { return CopyError::kResourceLimit;
  } catch (const std::length_error&) { return CopyError::kResourceLimit; }
}
}  // namespace abcd::source
