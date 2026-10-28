#ifndef ABCD_SOURCE_ITERATOR_CONTINUATION_H_
#define ABCD_SOURCE_ITERATOR_CONTINUATION_H_

#include <type_traits>

#include "abcd/source/tree_failure.h"
#include "control_skeleton.h"
#include "exception_control.h"

namespace abcd::source {
enum class ForOfPlacementError : std::uint8_t {
  kInput,
  kActivation,
  kContinuation,
  kCoverage,
  kPayload,
  kProtection,
  kCopyPlacement,
  kCorrespondence,
  kResourceLimit,
  kWorkLimit
};
struct ForOfPlacementFailure {
  ForOfPlacementError kind;
  std::optional<IteratorProtocolId> protocol;
  std::optional<analysis::CompletionEdge> edge;
  TreeCensusFailure activation;
};
struct IteratorProtection {
  std::optional<analysis::TryId> region;
  analysis::CatchPart part = analysis::CatchPart::kProtected;
  bool operator==(const IteratorProtection&) const = default;
};
struct IteratorContinuation {
  IteratorProtocolId protocol;
  LoopId loop;
  analysis::ExceptionNodeId header;
  analysis::CompletionEdge exhausted;
  analysis::ExceptionNodeId original_target;
  ControlId projected_target;
  ControlId legacy_continuation;
  ControlId retained_continuation;
  analysis::EdgeId edge;
  bool operator==(const IteratorContinuation&) const = default;
};
struct IteratorBodyCut {
  IteratorProtocolId protocol;
  analysis::CompletionEdge edge;
  analysis::PendingCompletion pending;
  bool operator==(const IteratorBodyCut&) const = default;
};
struct IteratorContinuationCoverage {
  IteratorProtocolId protocol;
  analysis::ExceptionNodeId node;
  auto operator<=>(const IteratorContinuationCoverage&) const = default;
};
// Exact original phi occurrence/edge. Scheduling remains the common phi/catch
// copy owner's job; lexical replanning never changes a copy's original port.
struct IteratorCopyPlacement {
  analysis::UseId use;
  analysis::EdgeId edge;
  analysis::ValueId destination;
  analysis::ValueOperand source;
  ExceptionTransfer transfer;
  analysis::WorkBlockId source_block, target_block;
  analysis::ExceptionPhase source_phase;
  bool operator==(const IteratorCopyPlacement& b) const {
    if (use != b.use || edge != b.edge || destination != b.destination ||
        transfer != b.transfer || source_block != b.source_block ||
        target_block != b.target_block || source_phase != b.source_phase ||
        source.index() != b.source.index())
      return false;
    return std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, analysis::SpecialValue>)
            return value == std::get<T>(b.source);
          else
            return value.value == std::get<T>(b.source).value;
        },
        source);
  }
};
struct IteratorContinuationProof {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  TreeCensusFailure activation;
  std::vector<IteratorContinuation> continuations;
  std::vector<IteratorBodyCut> cuts;
  std::vector<IteratorContinuationCoverage> coverage;
  std::vector<IteratorProtection> contexts, parents;
  std::vector<std::optional<analysis::TryId>> dispatch;
  std::vector<IteratorCopyPlacement> copies;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Sealed original owner facts retained separately from a proposed placement.
// They have no source control IDs and cannot choose a lexical continuation.
struct IteratorContinuationFacts {
  std::vector<IteratorBodyCut> cuts;
  std::vector<IteratorContinuationCoverage> coverage;
  std::vector<IteratorProtection> contexts, parents;
  std::vector<std::optional<analysis::TryId>> dispatch;
  std::vector<ExceptionTransfer> transfers;
};
class TreeCheckBudget;
struct OriginalFlow;
struct OriginalIterations;
void RetainIteratorContinuationFacts(const ExceptionPlan&,
                                     const VerifiedProtocolRegionOwnership&,
                                     const IteratorContinuationProof&,
                                     IteratorContinuationFacts&,
                                     TreeCheckBudget&);
void VerifyIteratorContinuationRecoveryFacts(const OriginalFlow&,
                                             const OriginalIterations&,
                                             TreeCheckBudget&);
// The real failed census is a necessary activation, not semantic permission.
// Discovery uses the independently sealed close boundaries and old lexical
// ownership. It neither changes adjacency nor owns any compiler close member.
std::variant<IteratorContinuationProof, ForOfPlacementFailure>
ProveIteratorContinuations(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&,
    const VerifiedProtocolRegionOwnership&, const ExceptionControl& legacy,
    const TreeCensusFailure&, analysis::SemanticBudget = {});
std::variant<std::uint64_t, ForOfPlacementFailure> VerifyIteratorContinuations(
    const abc::MethodBody&, const analysis::ValueProgram&,
    const analysis::PhasedValueInput&, const ExceptionPlan&,
    const VerifiedProtocolRegionOwnership&, const IteratorContinuationProof&,
    analysis::SemanticBudget = {});
// Replayed by operand/recovery consumers against their immutable retained raw
// inputs, in addition to the full original-phase verifier above.
struct IteratorContinuationInput {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::span<const analysis::WorkEdge> edges;
  std::span<const analysis::ValuePhaseSite> phases;
  std::span<const analysis::OperandUse> uses;
};
// Caller supplies an already admitted quota; no erased std::function
// allocation. Bounds/identity checks replay against raw immutable values, not
// builder flags.
template <class Step>
bool CheckIteratorContinuationInputs(const IteratorContinuationInput& g,
                                     const IteratorContinuationProof& p,
                                     Step&& step) {
  if (p.revision != g.revision || p.cfg_revision != g.cfg_revision ||
      p.continuations.empty())
    return false;
  for (const auto& c : p.continuations) {
    step(1);
    if (c.edge.value >= g.edges.size()) return false;
    const auto& e = g.edges[c.edge.value];
    if (e.source.value >= g.phases.size() ||
        e.target.value >= g.phases.size() ||
        g.phases[e.source.value].input != c.exhausted.source ||
        e.successor != c.exhausted.successor ||
        g.phases[e.target.value].input != c.original_target ||
        c.retained_continuation != c.projected_target)
      return false;
  }
  std::size_t copy = 0;
  for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
    step(1);
    const auto& use = g.uses[u];
    if (use.life != analysis::ValueLife::kLive || !use.incoming) continue;
    const auto* edge = std::get_if<analysis::EdgeId>(&*use.incoming);
    const auto* destination = std::get_if<analysis::ValueId>(&use.owner);
    if (!edge || !destination) continue;
    if (copy == p.copies.size()) return false;
    const auto& c = p.copies[copy++];
    if (edge->value >= g.edges.size() ||
        c.source_block != g.edges[edge->value].source ||
        c.target_block != g.edges[edge->value].target ||
        c.source_block.value >= g.phases.size() ||
        c.source_phase != g.phases[c.source_block.value].phase ||
        c.use != analysis::UseId{u} || c.edge != *edge ||
        c.destination != *destination ||
        c.source.index() != use.operand.index())
      return false;
    const bool same = std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, analysis::SpecialValue>)
            return value == std::get<T>(use.operand);
          else
            return value.value == std::get<T>(use.operand).value;
        },
        c.source);
    if (!same) return false;
  }
  return copy == p.copies.size();
}
inline IteratorContinuationInput ContinuationInput(
    const analysis::ValueProgram& g) {
  return {g.revision, g.cfg_revision, g.edges, g.phases, g.uses};
}
std::vector<LoopExitConstraint> IteratorContinuationConstraints(
    const analysis::ValueProgram&, const ExceptionControl&,
    const IteratorContinuationProof&, ControlQuota&);
}  // namespace abcd::source
#endif
