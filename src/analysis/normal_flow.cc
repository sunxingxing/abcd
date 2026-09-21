#include "abcd/analysis/normal_flow.h"

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <span>

namespace abcd::analysis {
namespace {
using abc::CodeOffset;
using abc::ControlTransfer;
using abc::MethodBody;

FlowFailure Failure(FlowError kind, CodeOffset offset = {}) {
  return {kind, offset};
}

std::optional<FlowFailure> CheckLayout(const MethodBody& body,
                                       std::uint32_t* code_bytes) {
  std::uint64_t next = 0;
  for (const auto& instruction : body.instructions) {
    const auto& decoded = instruction.decoded;
    const auto transfer = abc::GetControlTransfer(decoded.opcode);
    if (!transfer || instruction.offset.value != next || decoded.size == 0 ||
        decoded.size > abc::kMaxInstructionBytes)
      return Failure(FlowError::kMalformedBody, instruction.offset);
    next += decoded.size;
    if (next > std::numeric_limits<std::uint32_t>::max())
      return Failure(FlowError::kMalformedBody, instruction.offset);
    const bool jump = *transfer == ControlTransfer::kJump ||
                      *transfer == ControlTransfer::kBranch;
    if (jump != decoded.jump ||
        (*transfer == ControlTransfer::kBranch) != decoded.conditional ||
        jump != instruction.branch_target.has_value() ||
        jump != decoded.branch_delta.has_value())
      return Failure(FlowError::kMalformedBody, instruction.offset);
    if (jump) {
      const auto pc = static_cast<std::int64_t>(instruction.offset.value);
      const auto delta = *decoded.branch_delta;
      if (delta < -pc ||
          delta > std::numeric_limits<std::uint32_t>::max() - pc ||
          pc + delta != instruction.branch_target->value)
        return Failure(FlowError::kInvalidTarget, instruction.offset);
    }
  }
  *code_bytes = next;
  return std::nullopt;
}

class BlockLeaders {
 public:
  BlockLeaders(const MethodBody& body, std::uint32_t code_bytes,
               std::span<std::uint8_t> marked)
      : body_(body), code_bytes_(code_bytes), marked_(marked) {}

  bool Mark(CodeOffset offset, bool allow_end) {
    if (offset.value == code_bytes_) {
      if (!allow_end) return false;
      marked_.back() = true;
      return true;
    }
    const auto& instructions = body_.instructions;
    const auto found =
        std::lower_bound(instructions.begin(), instructions.end(), offset,
                         [](const auto& instruction, CodeOffset pc) {
                           return instruction.offset < pc;
                         });
    if (found == instructions.end() || found->offset != offset) return false;
    marked_[found - instructions.begin()] = true;
    return true;
  }

  bool Range(CodeOffset begin, std::uint32_t length, bool handler) {
    if (begin.value > code_bytes_ || length > code_bytes_ - begin.value)
      return false;
    return Mark(begin, !handler) &&
           Mark(CodeOffset{begin.value + length}, true);
  }

 private:
  const MethodBody& body_;
  std::uint32_t code_bytes_;
  std::span<std::uint8_t> marked_;
};

std::optional<FlowFailure> MarkLeaders(const MethodBody& body,
                                       std::uint32_t code_bytes,
                                       std::span<std::uint8_t> marked) {
  marked.front() = true;
  marked.back() = true;
  BlockLeaders leaders(body, code_bytes, marked);
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto& instruction = body.instructions[i];
    if (*abc::GetControlTransfer(instruction.decoded.opcode) !=
        ControlTransfer::kFallthrough)
      marked[i + 1] = true;
    if (instruction.branch_target &&
        !leaders.Mark(*instruction.branch_target, false))
      return Failure(FlowError::kInvalidTarget, instruction.offset);
  }
  // Keep exception boundaries without expanding I*H associations. A separate
  // exception-dispatch analysis must retain the encoded priority/order.
  for (const auto& region : body.tries) {
    if (region.first_handler > body.handlers.size() ||
        region.handler_count > body.handlers.size() - region.first_handler)
      return Failure(FlowError::kMalformedBody, region.offset);
    if (!leaders.Range(region.offset, region.length, false))
      return Failure(FlowError::kInvalidTarget, region.offset);
  }
  for (const auto& handler : body.handlers) {
    if (!leaders.Range(handler.offset, handler.length, true))
      return Failure(FlowError::kInvalidTarget, handler.offset);
  }
  return std::nullopt;
}

std::optional<BlockId> FindBlock(const NormalFlow& graph, CodeOffset target) {
  const auto found = std::lower_bound(
      graph.blocks.begin(), graph.blocks.end(), target,
      [](const FlowBlock& block, CodeOffset pc) { return block.begin < pc; });
  if (found == graph.blocks.end() || found->begin != target)
    return std::nullopt;
  return BlockId{static_cast<std::uint32_t>(found - graph.blocks.begin())};
}

std::optional<FlowFailure> ConnectBlocks(const MethodBody& body,
                                         NormalFlow* graph) {
  for (std::size_t i = 0; i < graph->blocks.size(); ++i) {
    auto& block = graph->blocks[i];
    const auto& last = body.instructions[block.first_instruction +
                                         block.instruction_count - 1];
    const auto append_next = [&](FlowEdgeKind kind) {
      if (i + 1 == graph->blocks.size()) return false;
      block.successors[block.successor_count++] = {
          BlockId{static_cast<std::uint32_t>(i + 1)}, kind};
      return true;
    };
    switch (block.transfer) {
      case ControlTransfer::kJump:
      case ControlTransfer::kBranch: {
        const auto target = FindBlock(*graph, *last.branch_target);
        if (!target) return Failure(FlowError::kInvalidTarget, last.offset);
        block.successors[block.successor_count++] = {*target,
                                                     FlowEdgeKind::kTaken};
        // Preserve both alternatives even when they share a destination.
        if (block.transfer == ControlTransfer::kBranch &&
            !append_next(FlowEdgeKind::kFallthrough))
          return Failure(FlowError::kFallthroughPastEnd, last.offset);
        break;
      }
      case ControlTransfer::kFallthrough:
      case ControlTransfer::kSuspend:
        if (!append_next(block.transfer == ControlTransfer::kSuspend
                             ? FlowEdgeKind::kResume
                             : FlowEdgeKind::kFallthrough))
          return Failure(FlowError::kFallthroughPastEnd, last.offset);
        break;
      case ControlTransfer::kReturn:
      case ControlTransfer::kThrow:
        break;
    }
  }
  return std::nullopt;
}

FlowResult Build(const MethodBody& body, FlowBudget budget) {
  if (body.instructions.empty()) return Failure(FlowError::kEmptyBody);
  if (body.instructions.size() >= std::numeric_limits<std::uint32_t>::max())
    return Failure(FlowError::kResourceLimit);
  std::uint32_t code_bytes = 0;
  if (const auto failure = CheckLayout(body, &code_bytes)) return *failure;
  const auto count = body.instructions.size();
  if (count + 1 > budget.bytes) return Failure(FlowError::kResourceLimit);
  std::vector<std::uint8_t> marked(count + 1);
  if (marked.capacity() > budget.bytes)
    return Failure(FlowError::kResourceLimit);
  if (const auto failure = MarkLeaders(body, code_bytes, marked))
    return *failure;
  const std::size_t blocks = std::count(marked.begin(), marked.end() - 1, true);
  if (blocks > (budget.bytes - marked.capacity()) / sizeof(FlowBlock))
    return Failure(FlowError::kResourceLimit);
  NormalFlow graph;
  // Count before reserve: no old/new graph buffers coexist, and untrusted
  // branch density cannot cause unaccounted geometric reallocation peaks.
  graph.blocks.reserve(blocks);
  if (graph.blocks.capacity() >
      (budget.bytes - marked.capacity()) / sizeof(FlowBlock))
    return Failure(FlowError::kResourceLimit);
  std::uint32_t first = 0;
  for (std::uint32_t i = 1; i <= count; ++i) {
    if (!marked[i]) continue;
    FlowBlock block;
    block.begin = body.instructions[first].offset;
    block.end =
        i == count ? CodeOffset{code_bytes} : body.instructions[i].offset;
    block.first_instruction = first;
    block.instruction_count = i - first;
    block.transfer =
        *abc::GetControlTransfer(body.instructions[i - 1].decoded.opcode);
    graph.blocks.push_back(block);
    first = i;
  }
  if (const auto failure = ConnectBlocks(body, &graph)) return *failure;
  return graph;
}
}  // namespace

FlowResult BuildNormalFlow(const abc::MethodBody& body, FlowBudget budget) {
  try {
    return Build(body, budget);
  } catch (const std::bad_alloc&) {
    return Failure(FlowError::kResourceLimit);
  }
}

}  // namespace abcd::analysis
