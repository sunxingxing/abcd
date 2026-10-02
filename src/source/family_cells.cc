#include "family_cells.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<FamilyCellCost, CaptureCellError> PlanFamilyCells(
    FileFamily& family, analysis::SemanticBudget budget) {
  using Error = CaptureCellError;
  auto left = budget;
  const auto require = [](bool condition, Error error) { if (!condition) throw error; };
  const auto step = [&](std::uint64_t count = 1) {
    require(count <= left.work, Error::kWorkLimit); left.work -= count;
  };
  const auto admit = [&](std::size_t count, std::size_t width) {
    require(count <= left.bytes / width, Error::kResourceLimit); left.bytes -= count * width;
  };
  try {
    admit(family.members.size(), sizeof(std::vector<CellDemand>));
    std::vector<std::vector<CellDemand>> demands(family.members.size());
    for (auto& member : family.members) { step(); member.function.ClearStorage(); }
    for (std::size_t i = family.members.size(); i-- > 0;) {
      step(); auto& member = family.members[i]; auto& function = member.function;
      const auto* capture = function.Defaults() ? function.Defaults()->Capture() : nullptr;
      const auto entries = capture ? std::span<const IncomingCell>(&capture->storage,1) : std::span<const IncomingCell>{};
      if (!demands[i].empty() || !entries.empty()) {
        const auto base = function.Capture();
        require(base.Environments() != nullptr, Error::kCell);
        auto result = function.Phase() ?
            BuildDemandedPhasedCellPlan(function.Graph(), *base.Environments(), demands[i], left) :
            BuildDemandedCellPlan(function.Graph(), *base.Environments(), demands[i], left, entries);
        if (const auto* error = std::get_if<CellError>(&result)) {
          if (*error == CellError::kResourceLimit) throw Error::kResourceLimit;
          if (*error == CellError::kWorkLimit) throw Error::kWorkLimit;
          throw Error::kCell;
        }
        auto cells = std::get<CellPlan>(std::move(result));
        admit(cells.allocated_bytes, 1); step(cells.work);
        const auto original = base.Input().Captures();
        std::size_t forwarded = 0;
        for (const auto& cell : cells.cells) {
          step(); forwarded += cell.count == 0 && std::holds_alternative<IncomingCell>(cell.storage);
        }
        admit(original.size() + forwarded, sizeof(CaptureUse));
        CaptureStorage storage{std::move(cells), {}};
        storage.uses.reserve(original.size() + forwarded);
        for (const auto& use : original) { step(); storage.uses.push_back(use); }
        for (const auto& cell : storage.cells.cells) {
          step();
          if (const auto* incoming = std::get_if<IncomingCell>(&cell.storage); incoming && cell.count == 0)
            storage.uses.push_back({{incoming->depth, incoming->slot}, analysis::CaptureDomain::kLanguage});
        }
        function.SetStorage(std::move(storage));
      }
      if (i == 0) continue;
      require(member.incoming && member.incoming->parent.value < i, Error::kOwner);
      const auto& edge = *member.incoming;
      const auto& parent = family.members[edge.parent.value].function;
      const auto& graph = parent.Graph();
      auto creation = edge.phase_operation;
      if (!creation) {
        require(edge.site.value < graph.input.sites.size(), Error::kCreation);
        creation = graph.input.sites[edge.site.value];
      }
      require(creation.has_value(), Error::kCreation);
      if (const auto* cells = function.Cells()) {
        auto& destination = demands[edge.parent.value];
        for (const auto& cell : cells->cells) {
          step(); const auto* incoming = std::get_if<IncomingCell>(&cell.storage);
          if (!incoming) continue;
          if (destination.size() == destination.capacity()) {
            // Geometric growth; cost proportional to retained transitive demand,
            // not one graph walk per child or per captured variable.
            const auto capacity = destination.empty() ? 1 : destination.size() * 2;
            require(capacity > destination.size(), Error::kResourceLimit);
            admit(capacity, sizeof(CellDemand)); destination.reserve(capacity);
          }
          destination.push_back({*creation, {incoming->depth, incoming->slot}});
        }
      }
    }
    return FamilyCellCost{budget.bytes - left.bytes, budget.work - left.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
