#ifndef ABCD_ANALYSIS_AWAIT_CATCH_FLOW_H_
#define ABCD_ANALYSIS_AWAIT_CATCH_FLOW_H_
#include "abcd/analysis/raw_event_graph.h"
namespace abcd::analysis {
// Original-body identities only. No normalized PC or source expression exists
// at this boundary. Catch and ordinary entry at the same PC are distinct.
using CatchFlowPhase = RawEventPhase;
enum class CatchFlowRole : std::uint8_t { kResult, kResumeMode };
enum class CatchFlowOutcome : std::uint8_t {
  kUnobserved, kCovered, kTerminal, kCoveredAndTerminal, kObservedRead, kUnknownEffect
};
enum class CatchFlowError : std::uint8_t {
  kRegion, kRegister, kTarget, kProtocol, kCorrespondence, kResourceLimit, kWorkLimit
};
struct CatchFlowFailure {
  CatchFlowError kind;
  std::optional<abc::CodeOffset> pc;
  bool operator==(const CatchFlowFailure&) const = default;
};
struct CatchFlowQuery {
  abc::CodeOffset await, definition, handler;
  abc::RegisterId slot;
  CatchFlowRole role;
  bool operator==(const CatchFlowQuery&) const = default;
};
struct CatchFlowEvidence {
  CatchFlowQuery query;
  CatchFlowOutcome outcome = CatchFlowOutcome::kUnobserved;
  std::optional<abc::CodeOffset> read, unknown, cover, terminal;
  bool operator==(const CatchFlowEvidence&) const = default;
};
struct CatchFlowState {
  std::uint32_t node;
  abc::RegisterId slot;
  auto operator<=>(const CatchFlowState&) const = default;
};
struct AwaitCatchFlowProof {
  std::vector<CatchFlowState> states;
  std::vector<CatchFlowEvidence> queries;
  std::size_t peak_bytes = 0;
  std::uint64_t work = 0;
};
struct CatchFlowCost { std::size_t peak_bytes; std::uint64_t work; };
// A diagnostic payload, separately owned and charged; it does not authorize
// normalization or effectful IteratorClose projection. Each query is fixed-size.
struct AwaitCatchFlowDiagnostic {
  std::vector<CatchFlowEvidence> queries;
  std::optional<CatchFlowFailure> failure;
  bool verified = false;
  bool operator==(const AwaitCatchFlowDiagnostic&) const = default;
};
// Revalidates supplied AwaitRegions on the immutable original body. All raw
// effects/dispatch and register Commit points remain present. Sparse state
// expansion has a linear admission quota; exhaustion is a resource refusal,
// never deadness. O((V+E) log V) admitted work, O(V+E) admitted storage.
[[nodiscard]] std::variant<AwaitCatchFlowProof, CatchFlowFailure> ProveAwaitCatchFlow(
    const abc::MethodBody&, std::span<const AwaitRegion>, abc::ProfileId,
    std::uint64_t frame_size, abc::CodeOffset completion_handler, SemanticBudget = {});
// Rebuilds original effects and dispatch; checks the supplied closure, every
// successor, query root and summary. Does not trust builder coverage/kill flags.
[[nodiscard]] std::variant<CatchFlowCost, CatchFlowFailure> VerifyAwaitCatchFlow(
    const abc::MethodBody&, std::span<const AwaitRegion>, abc::ProfileId,
    std::uint64_t frame_size, abc::CodeOffset completion_handler,
    const AwaitCatchFlowProof&, SemanticBudget = {});
} // namespace abcd::analysis
#endif
