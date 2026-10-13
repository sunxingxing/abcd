#ifndef ABCD_SOURCE_DEFERRED_FAILURE_H_
#define ABCD_SOURCE_DEFERRED_FAILURE_H_
#include "capture_cells.h"
namespace abcd::source {
// Failure-only linear scan of already charged occurrence facts. It does not
// rebuild binding or choose another store. Only the rejected requirement is
// reported; no additional successful-path storage or admission is introduced.
inline DeferredCellEvidence DescribeDeferredFailure(DeferredCellError reason,
    const analysis::ValueProgram& graph, const CellPlan* cells, const CaptureCells& capture,
    const DeferredCellInitialization* entry, std::span<const CaptureUse> uses) {
  DeferredCellEvidence result{reason}; result.creator = capture.parent; result.child = capture.child;
  result.creation = OperationLocation(graph, capture.creation, capture.parent);
  if (!entry) return result;
  result.cell = entry->cell; result.store = OperationLocation(graph, entry->store, capture.parent);
  if (cells && entry->cell.value < cells->cells.size())
    if (const auto* allocated = std::get_if<AllocatedCell>(&cells->cells[entry->cell.value].storage))
      result.allocation = OperationLocation(graph, allocated->allocation, capture.parent);
  if (entry->store.value >= graph.ops.size()) return result;
  const auto origin = graph.ops[entry->store.value].origin;
  if (origin.value >= graph.origins.size()) return result;
  const auto site = graph.origins[origin.value].site.value;
  for (std::size_t i = 0; i < capture.input.bindings.size(); ++i) {
    const auto& binding = capture.input.bindings[i];
    if (!binding.deferred || binding.deferred->value != site) continue;
    result.requested = binding.domain;
    if (i < uses.size()) result.occurrence = CaptureOccurrence{static_cast<std::uint32_t>(i),
        uses[i].cell, binding.domain, uses[i].source};
    break;
  }
  return result;
}
}
#endif
