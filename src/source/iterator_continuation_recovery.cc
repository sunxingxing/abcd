#include "iterator_continuation.h"
#include "original_flow.h"

namespace abcd::source {
namespace {
IteratorProtection Context(std::optional<analysis::CatchScope> scope) {
  return scope ? IteratorProtection{scope->region, scope->part}
               : IteratorProtection{};
}
}  // namespace
void RetainIteratorContinuationFacts(
    const ExceptionPlan& regions, const VerifiedProtocolRegionOwnership& owner,
    const IteratorContinuationProof& proof, IteratorContinuationFacts& facts,
    TreeCheckBudget& budget) {
  // Original close/region owners supply these facts independently of the
  // placement producer. Recovery compares the candidate certificate against
  // this immutable raw input, then checks actual native completion actions.
  std::vector<std::uint8_t> selected;
  budget.Array(selected, owner.Scan().candidates.size());
  for (const auto& c : proof.continuations) {
    budget.Step();
    RequireTree(c.protocol.value < selected.size(),
                TreeStructureError::kOwnership);
    selected[c.protocol.value] = 1;
  }
  const auto& summary = owner.Summary();
  std::size_t count = 0;
  for (const auto& r : summary.regions) {
    budget.Step();
    if (!selected[r.protocol.value]) continue;
    RequireTree(r.boundary.has_value(), TreeStructureError::kOwnership);
    for (const auto& range : r.boundary->protected_ranges) {
      budget.Step();
      if (range.role == ProtectedRangeRole::kLoopBody) {
        RequireTree(range.ordinary.size() <= SIZE_MAX - count,
                    TreeStructureError::kResourceLimit);
        count += range.ordinary.size();
      }
    }
  }
  budget.Array(facts.coverage, count);
  std::size_t at = 0;
  for (const auto& r : summary.regions)
    if (selected[r.protocol.value])
      for (const auto& range : r.boundary->protected_ranges)
        if (range.role == ProtectedRangeRole::kLoopBody)
          for (const auto node : range.ordinary) {
            budget.Step();
            facts.coverage[at++] = {r.protocol, node};
          }
  std::sort(facts.coverage.begin(), facts.coverage.end(),
            [&](const auto& a, const auto& b) {
              budget.Step();
              return a < b;
            });
  facts.coverage.erase(
      std::unique(facts.coverage.begin(), facts.coverage.end()),
      facts.coverage.end());
  budget.Array(facts.cuts, summary.entries.size());
  at = 0;
  for (const auto& e : summary.entries) {
    budget.Step();
    if (selected[e.protocol.value])
      facts.cuts[at++] = {e.protocol, e.source, e.pending};
  }
  facts.cuts.resize(at);
  budget.Array(facts.transfers, regions.transfers.size());
  for (std::size_t n = 0; n < facts.transfers.size(); ++n) {
    budget.Step();
    facts.transfers[n] = regions.transfers[n];
  }
  budget.Array(facts.contexts, regions.layout.scopes.size());
  for (std::size_t n = 0; n < facts.contexts.size(); ++n) {
    budget.Step();
    facts.contexts[n] = Context(regions.layout.scopes[n]);
  }
  budget.Array(facts.parents, regions.layout.regions.size());
  budget.Array(facts.dispatch, regions.layout.regions.size());
  for (std::size_t n = 0; n < facts.parents.size(); ++n) {
    budget.Step();
    facts.parents[n] = Context(regions.layout.regions[n].parent);
    facts.dispatch[n] = regions.layout.regions[n].outer_dispatch;
  }
}
void VerifyIteratorContinuationRecoveryFacts(const OriginalFlow& raw,
                                             const OriginalIterations& native,
                                             TreeCheckBudget& budget) {
  if (!native.view.continuations) {
    RequireTree(!native.continuation_facts, TreeStructureError::kOwnership);
    return;
  }
  RequireTree(native.continuation_facts.has_value(),
              TreeStructureError::kOwnership);
  const auto& p = *native.view.continuations;
  const auto& facts = *native.continuation_facts;
  budget.Step(p.coverage.size() + p.cuts.size() + p.contexts.size() +
              p.parents.size() + p.dispatch.size());
  RequireTree(facts.coverage == p.coverage && facts.cuts == p.cuts &&
                  facts.contexts == p.contexts && facts.parents == p.parents &&
                  facts.dispatch == p.dispatch,
              TreeStructureError::kOwnership);
  for (const auto& copy : p.copies) {
    budget.Step();
    RequireTree(copy.edge.value < facts.transfers.size() &&
                    copy.transfer == facts.transfers[copy.edge.value],
                TreeStructureError::kOwnership);
  }
  const IteratorContinuationInput input{raw.value_revision, raw.cfg_revision,
                                        raw.edges, raw.phases, raw.uses};
  RequireTree(CheckIteratorContinuationInputs(
                  input, p, [&](std::uint64_t n) { budget.Step(n); }),
              TreeStructureError::kOwnership);
}
}  // namespace abcd::source
