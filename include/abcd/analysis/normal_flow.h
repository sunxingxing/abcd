#ifndef ABCD_ANALYSIS_NORMAL_FLOW_H_
#define ABCD_ANALYSIS_NORMAL_FLOW_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/method_body.h"

namespace abcd::analysis {

struct BlockId {
  std::uint32_t value = 0;
  auto operator<=>(const BlockId&) const = default;
};

enum class FlowEdgeKind : std::uint8_t { kTaken, kFallthrough, kResume };

struct FlowEdge {
  BlockId target;
  FlowEdgeKind kind = FlowEdgeKind::kFallthrough;
};

inline constexpr std::size_t kMaxNormalSuccessors = 2;

struct FlowBlock {
  abc::CodeOffset begin;
  abc::CodeOffset end;
  std::uint32_t first_instruction = 0;
  std::uint32_t instruction_count = 0;
  abc::ControlTransfer transfer = abc::ControlTransfer::kFallthrough;
  std::uint8_t successor_count = 0;
  std::array<FlowEdge, kMaxNormalSuccessors> successors{};
};

struct NormalFlow {
  std::vector<FlowBlock> blocks;
};

enum class FlowError {
  kEmptyBody,
  kMalformedBody,
  kInvalidTarget,
  kFallthroughPastEnd,
  kResourceLimit,
};

struct FlowFailure {
  FlowError kind;
  abc::CodeOffset offset;
};

struct FlowBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

using FlowResult = std::variant<NormalFlow, FlowFailure>;

// Owns block/edge storage, not instructions. Exception range boundaries are
// preserved but exception dispatch edges are NOT part of this normal graph.
// A resume edge denotes a possible later activation, not immediate execution.
// Budget covers additional graph/scratch storage; caller retains body budget.
[[nodiscard]] FlowResult BuildNormalFlow(const abc::MethodBody& body,
                                         FlowBudget budget = {});

}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_NORMAL_FLOW_H_
