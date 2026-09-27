#ifndef ABCD_ANALYSIS_EXCEPTION_FLOW_H_
#define ABCD_ANALYSIS_EXCEPTION_FLOW_H_

#include <array>
#include "abcd/analysis/exception_edge.h"

namespace abcd::analysis {
struct ExceptionNodeId {
  std::uint32_t value = 0;
  auto operator<=>(const ExceptionNodeId&) const = default;
};
enum class ExceptionPhase : std::uint8_t { kBefore, kCommit, kCatch };
enum class ExceptionArcKind : std::uint8_t {
  kNormal, kTaken, kFallthrough, kException
};
struct ExceptionArc {
  ExceptionNodeId target;
  ExceptionArcKind kind;
};
struct ExceptionNode {
  InstructionId instruction;
  ExceptionPhase phase;
  std::array<ExceptionArc, kMaxNormalSuccessors> successors{};
  std::uint8_t successor_count = 0;
  std::optional<ExceptionEdge> exception;
};
struct ExceptionFlow {
  std::vector<SemanticInstruction> instructions;
  std::vector<ExceptionNode> nodes;
  std::size_t allocated_bytes = 0;
  // Charged lifting/connection work; upstream byte-bounded validation excluded.
  std::uint64_t work_units = 0;
};
using ExceptionFlowResult = std::variant<ExceptionFlow, SemanticFailure,
                                         FlowFailure, DispatchFailure>;
struct ExceptionFlowBudget {
  std::size_t bytes = 64 * 1024 * 1024;
  // Lifting and edge construction only. Existing normal-flow/dispatch
  // validation has its own byte admission, not this work counter.
  std::uint64_t graph_work = kDefaultSemanticWork;
};

// Before nodes read inputs and execute effects; only Commit nodes define the
// normal register destination. Exception arcs bypass Commit and enter a Catch
// node defining acc from the edge's exception. Other registers retain their
// pre-instruction values. A normal jump to handler code bypasses Catch.
// No lexical environment restoration or exception-aware SSA is claimed here.
// AwaitSite certificates select the same normalized source effects as normal
// CFG lifting. Exception arcs bypass their result Commit. This models the
// supplied normalized body, not equivalence to the original resume protocol:
// the owner must separately prove handler routing and observable register
// snapshots on rejected awaits. Raw suspension/resume still rejects. Async
// enter/settle can be represented without certifying their protocol owner;
// value/source admission must reject them until that separate proof exists.
// Total O((I+R+H) log(I+1) + R log(R+1) + I log(R+1) + E) time;
// O(I+H+R) memory. bytes includes simultaneous scratch, excludes input body.
// graph_work bounds lifting/connection passes and lookup comparisons, not the
// byte-bounded upstream normal-flow/dispatch construction listed above.
[[nodiscard]] ExceptionFlowResult BuildExceptionFlow(
    const abc::MethodBody& body, abc::ProfileId profile,
    std::uint64_t frame_size, ExceptionFlowBudget budget = {},
    std::span<const AwaitSite> awaits = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_EXCEPTION_FLOW_H_
