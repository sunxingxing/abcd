#include "exception_copies.h"

#include <new>
#include <stdexcept>
#include "edge_copy_index.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ExceptionCopyError;
void Require(bool condition, Error error) { if (!condition) throw error; }
Error CopyFailure(CopyError error) {
  if (error == CopyError::kWorkLimit) return Error::kWorkLimit;
  if (error == CopyError::kResourceLimit) return Error::kResourceLimit;
  return Error::kPhi;
}
void CheckCopies(const ValueProgram& graph, const PhiCopies& copies, CopyBudget& budget) {
  Require(graph.values.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
  const auto checked = VerifyPhasedPhiCopies(graph, copies, budget.Remaining());
  if (const auto* error = std::get_if<PhiCopyError>(&checked)) {
    if (*error == PhiCopyError::kWorkLimit) throw Error::kWorkLimit;
    if (*error == PhiCopyError::kResourceLimit) throw Error::kResourceLimit;
    throw Error::kPhi;
  }
  budget.Step(std::get<std::uint64_t>(checked));
}
std::optional<WorkBlockId> CatchPhi(const ValueProgram& graph, const ValueDefinition& value) {
  const auto* phi = std::get_if<PhiDefinition>(&value.origin);
  if (value.life != ValueLife::kLive || !phi) return {};
  Require(phi->block.value < graph.phases.size(), Error::kPhi);
  if (graph.phases[phi->block.value].phase != ExceptionPhase::kCatch) return {};
  return phi->block;
}
bool SlotLess(const CatchSlot& a, const CatchSlot& b) {
  return a.block != b.block ? a.block < b.block : a.destination < b.destination;
}
CopyPlacement Placement(const ValueProgram& graph, const PhiBundle& bundle) {
  const auto* id = std::get_if<EdgeId>(&bundle.incoming);
  if (!id) return EntryCopies{};
  Require(id->value < graph.edges.size(), Error::kPlacement);
  const auto& edge = graph.edges[id->value];
  if (edge.kind != FlowEdgeKind::kException) return NormalCopies{*id};
  const auto& before = graph.phases[edge.source.value];
  Require(before.phase == ExceptionPhase::kBefore && before.operation &&
      graph.phases[edge.target.value].phase == ExceptionPhase::kCatch, Error::kPlacement);
  return ThrowCopies{*id, *before.operation, edge.target};
}
}  // namespace

std::variant<ExceptionCopies, ExceptionCopyError> BuildExceptionCopies(
    const ValueProgram& graph, const PhiCopies& copies, SemanticBudget allowance) {
  try {
    CopyBudget budget(allowance);
    CheckCopies(graph, copies, budget);
    ExceptionCopies plan{graph.revision, graph.cfg_revision};
    budget.Admit(1, sizeof(plan));
    budget.Array(plan.placements, copies.bundles.size());
    budget.Array(plan.staging, copies.copies.size());
    std::size_t count = 0;
    for (const auto& value : graph.values) { budget.Step(); count += CatchPhi(graph, value).has_value(); }
    budget.Reserve(plan.slots, count);
    budget.Reserve(plan.commits, count);
    for (std::size_t v = 0; v < graph.values.size(); ++v) {
      budget.Step();
      if (const auto block = CatchPhi(graph, graph.values[v]))
        plan.slots.push_back({*block, ValueId{static_cast<std::uint32_t>(v)}});
    }
    std::sort(plan.slots.begin(), plan.slots.end(), [&](const auto& a, const auto& b) {
      budget.Step(); return SlotLess(a, b);
    });
    std::vector<std::optional<CatchSlotId>> slots;
    budget.Array(slots, graph.values.size());
    for (std::size_t s = 0; s < plan.slots.size(); ++s) {
      budget.Step();
      const auto& slot = plan.slots[s];
      const CatchSlotId id{static_cast<std::uint32_t>(s)};
      slots[slot.destination.value] = id;
      if (plan.commits.empty() || plan.commits.back().block != slot.block)
        plan.commits.push_back({slot.block, id, 1});
      else ++plan.commits.back().count;
    }
    for (std::size_t b = 0; b < copies.bundles.size(); ++b) {
      budget.Step();
      const auto& bundle = copies.bundles[b];
      plan.placements[b] = Placement(graph, bundle);
      if (!std::holds_alternative<ThrowCopies>(plan.placements[b])) continue;
      for (std::size_t i = bundle.first; i < std::size_t(bundle.first) + bundle.count; ++i) {
        budget.Step();
        const auto slot = slots[copies.copies[i].destination.value];
        Require(slot.has_value(), Error::kSlot);
        plan.staging[i] = slot;
      }
    }
    const auto checked = VerifyExceptionCopies(graph, copies, plan, budget.Remaining());
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    budget.Step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = sizeof(plan) + plan.placements.capacity() * sizeof(CopyPlacement) +
        plan.staging.capacity() * sizeof(std::optional<CatchSlotId>) +
        plan.slots.capacity() * sizeof(CatchSlot) + plan.commits.capacity() * sizeof(CatchCommit);
    plan.work = budget.Work();
    return plan;
  } catch (Error error) { return error;
  } catch (CopyError error) { return CopyFailure(error);
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, ExceptionCopyError> VerifyExceptionCopies(
    const ValueProgram& graph, const PhiCopies& copies, const ExceptionCopies& plan,
    SemanticBudget allowance) {
  try {
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    CopyBudget budget(allowance);
    CheckCopies(graph, copies, budget);
    Require(plan.placements.size() == copies.bundles.size(), Error::kPlacement);
    Require(plan.staging.size() == copies.copies.size(), Error::kSlot);
    std::vector<std::optional<CatchSlotId>> slots;
    budget.Array(slots, graph.values.size());
    Require(plan.slots.size() <= graph.values.size(), Error::kSlot);
    for (std::size_t s = 0; s < plan.slots.size(); ++s) {
      budget.Step();
      const auto& slot = plan.slots[s];
      Require(slot.destination.value < graph.values.size() &&
          CatchPhi(graph, graph.values[slot.destination.value]) == slot.block &&
          !slots[slot.destination.value] && (!s || SlotLess(plan.slots[s - 1], slot)), Error::kSlot);
      slots[slot.destination.value] = CatchSlotId{static_cast<std::uint32_t>(s)};
    }
    for (std::size_t v = 0; v < graph.values.size(); ++v) {
      budget.Step();
      Require(slots[v].has_value() == CatchPhi(graph, graph.values[v]).has_value(), Error::kSlot);
    }
    std::size_t first = 0;
    std::optional<WorkBlockId> previous;
    for (const auto& commit : plan.commits) {
      budget.Step();
      Require(commit.first.value == first && commit.count && commit.count <= plan.slots.size() - first &&
          (!previous || *previous < commit.block), Error::kCommit);
      for (std::size_t i = first; i < first + commit.count; ++i) {
        budget.Step(); Require(plan.slots[i].block == commit.block, Error::kCommit);
      }
      first += commit.count;
      previous = commit.block;
    }
    Require(first == plan.slots.size(), Error::kCommit);
    for (std::size_t b = 0; b < copies.bundles.size(); ++b) {
      budget.Step();
      const auto& bundle = copies.bundles[b];
      Require(plan.placements[b] == Placement(graph, bundle), Error::kPlacement);
      const auto* raised = std::get_if<ThrowCopies>(&plan.placements[b]);
      for (std::size_t i = bundle.first; i < std::size_t(bundle.first) + bundle.count; ++i) {
        budget.Step();
        if (!raised) { Require(!plan.staging[i], Error::kSlot); continue; }
        const auto slot = slots[copies.copies[i].destination.value];
        Require(slot && plan.staging[i] == slot && plan.slots[slot->value].block == raised->handler, Error::kSlot);
      }
    }
    return budget.Work();
  } catch (Error error) { return error;
  } catch (CopyError error) { return CopyFailure(error);
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
