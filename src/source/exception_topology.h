#ifndef ABCD_SOURCE_EXCEPTION_TOPOLOGY_H_
#define ABCD_SOURCE_EXCEPTION_TOPOLOGY_H_

#include "control_skeleton.h"
#include "exception_control.h"

namespace abcd::source {
struct TryControlPort {
  analysis::WorkBlockId target;
  ControlNodeId destination;
};
// Private projection owner. Scratch/result grants cover both this topology and
// its mapping while the shared skeleton is discovered or independently checked.
struct ExceptionTopology {
  std::vector<ControlVertex> blocks;
  std::vector<ControlLink> edges;
  std::vector<ControlId> entries;
  std::vector<std::optional<analysis::TryId>> owners;
  std::vector<std::vector<TryControlPort>> ports;
  std::vector<ScopedControlEntry> scoped_entries;
  ControlLoopLayout loops;
  ExceptionControl mapping;
};
std::vector<std::optional<analysis::CatchScope>> ProjectExceptionScopes(
    const analysis::ValueProgram&, const ExceptionPlan&,
    std::span<const std::uint8_t> retained, ControlQuota&);
ExceptionTopology ProjectExceptionControl(const analysis::ValueProgram& graph,
    const ExceptionPlan& regions, ControlQuota& quota);
ExceptionTopology ProjectProtocolControl(const analysis::ValueProgram&, const ExceptionPlan&,
    const analysis::PhasedValueInput&, const VerifiedProtocolRegionOwnership&, ControlQuota&,
    std::shared_ptr<const IteratorContinuationProof> = {});
// Wrap an already sealed control projection with the remaining ordinary tries.
// Original operations/edges remain in graph; no synthetic ValueProgram is made.
ExceptionTopology ComposeOrdinaryEnclosures(ExceptionTopology, const ExceptionPlan&,
    const analysis::ValueProgram&, ControlQuota&);
// Borrowed lexical wrapper layout, not an ExceptionPlan or dispatch proof.
// Original TryIds/ranges remain stable; parents may skip sealed dispatch units.
struct TryLoopLayout {
  std::span<const analysis::TryId> order;
  std::span<const analysis::CatchRegion> regions;
};
void ComposeExceptionLoops(ExceptionTopology& topology, TryLoopLayout regions,
    const analysis::ValueProgram& graph, ControlQuota& quota);
}  // namespace abcd::source
#endif
