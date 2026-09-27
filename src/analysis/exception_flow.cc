#include "abcd/analysis/exception_flow.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include "semantic_lifter.h"

namespace abcd::analysis {
namespace {
constexpr std::size_t kInstructionPhases = 2;
struct WorkLimit {};
ExceptionNodeId Before(std::size_t instruction) {
  return {static_cast<std::uint32_t>(instruction * kInstructionPhases)};
}
ExceptionNodeId Commit(std::size_t instruction) {
  return {static_cast<std::uint32_t>(instruction * kInstructionPhases + 1)};
}
SemanticFailure Resource() { return {SemanticError::kResourceLimit, {}}; }
}

ExceptionFlowResult BuildExceptionFlow(const abc::MethodBody& body,
    abc::ProfileId profile, std::uint64_t frame_size, ExceptionFlowBudget budget,
    std::span<const AwaitSite> awaits, const abc::FrameLayout* frame) {
  if (frame && frame->registers != frame_size)
    return SemanticFailure{SemanticError::kMalformedInstruction, {}};
  auto normal = BuildNormalFlow(body, {budget.bytes});
  if (const auto* failure = std::get_if<FlowFailure>(&normal)) return *failure;
  const auto& graph = std::get<NormalFlow>(normal);
  const auto normal_bytes = graph.blocks.capacity() * sizeof(FlowBlock);
  if (normal_bytes > budget.bytes) return Resource();
  auto available = budget.bytes - normal_bytes;
  const auto selected = BuildExceptionDispatch(body, profile, {available});
  if (const auto* failure = std::get_if<DispatchFailure>(&selected)) return *failure;
  const auto& dispatch = std::get<ExceptionDispatch>(selected);
  const auto dispatch_bytes = dispatch.spans.capacity() * sizeof(DispatchSpan);
  if (dispatch_bytes > available) return Resource();
  available -= dispatch_bytes;
  const auto count = body.instructions.size();
  const auto catches = std::min(count, body.handlers.size());
  constexpr auto kNodeLimit = std::numeric_limits<std::uint32_t>::max();
  if (catches > kNodeLimit || count > (kNodeLimit - catches) / kInstructionPhases)
    return Resource();
  const auto node_limit = count * kInstructionPhases + catches;
  if (node_limit > available / sizeof(ExceptionNode)) return Resource();
  available -= node_limit * sizeof(ExceptionNode);
  constexpr auto kInstructionBytes = sizeof(SemanticInstruction) +
      sizeof(std::optional<ExceptionNodeId>);
  if (count > available / kInstructionBytes) return Resource();
  try {
    auto work = budget.graph_work;
    const auto charge = [&](std::uint64_t units = 1) {
      if (units > work) throw WorkLimit{};
      work -= units;
    };
    ExceptionFlow result;
    if (frame) result.frame = *frame;
    result.nodes.reserve(node_limit);
    result.instructions.reserve(count);
    std::vector<std::optional<ExceptionNodeId>> entries(count);
    if (result.nodes.capacity() > node_limit ||
        result.instructions.capacity() > count || entries.capacity() > count)
      return Resource();
    detail::SemanticLifter lifter{profile, frame_size, awaits, frame};
    for (std::size_t i = 0; i < count; ++i) {
      charge();
      auto lifted = lifter.Lift(body.instructions[i]);
      if (const auto* failure = std::get_if<SemanticFailure>(&lifted)) return *failure;
      result.instructions.push_back(std::get<SemanticInstruction>(std::move(lifted)));
      const InstructionId instruction{static_cast<std::uint32_t>(i)};
      result.nodes.push_back({instruction, ExceptionPhase::kBefore});
      result.nodes.push_back({instruction, ExceptionPhase::kCommit});
    }
    if (!lifter.Complete())
      return SemanticFailure{SemanticError::kMalformedInstruction, {}};
    const auto find = [&](abc::CodeOffset pc) -> std::optional<std::size_t> {
      const auto it = std::lower_bound(body.instructions.begin(), body.instructions.end(), pc,
          [&](const auto& site, auto target) {
            charge();
            return site.offset < target;
          });
      if (it == body.instructions.end() || it->offset != pc) return {};
      return static_cast<std::size_t>(it - body.instructions.begin());
    };
    const auto connect = [&](ExceptionNodeId from, ExceptionNodeId to,
                              ExceptionArcKind kind) {
      charge();
      auto& node = result.nodes[from.value];
      node.successors[node.successor_count++] = {to, kind};
    };
    for (std::size_t i = 0; i < count; ++i) {
      charge(1 + std::bit_width(dispatch.spans.size()));
      const auto& instruction = result.instructions[i];
      const auto before = Before(i);
      const auto commit = Commit(i);
      if (instruction.effects.exceptional != ExceptionalCompletion::kAlwaysThrow)
        connect(before, commit, ExceptionArcKind::kNormal);
      if (const auto edge = ResolveExceptionEdge(instruction, dispatch)) {
        result.nodes[before.value].exception = edge;
        if (edge->target) {
          const auto target = find(edge->target->handler_pc);
          if (!target) return FlowFailure{FlowError::kInvalidTarget, instruction.pc};
          if (!entries[*target]) {
            if (result.nodes.size() == node_limit) return Resource();
            const ExceptionNodeId entry{static_cast<std::uint32_t>(result.nodes.size())};
            entries[*target] = entry;
            result.nodes.push_back({InstructionId{static_cast<std::uint32_t>(*target)},
                                    ExceptionPhase::kCatch});
            connect(entry, Before(*target), ExceptionArcKind::kNormal);
          }
          connect(before, *entries[*target], ExceptionArcKind::kException);
        }
      }
    }
    for (const auto& block : graph.blocks) {
      charge();
      const auto end = std::size_t(block.first_instruction) + block.instruction_count;
      for (std::size_t i = block.first_instruction; i + 1 < end; ++i)
        connect(Commit(i), Before(i + 1), ExceptionArcKind::kFallthrough);
      for (std::size_t e = 0; e < block.successor_count; ++e) {
        const auto& edge = block.successors[e];
        if (edge.kind == FlowEdgeKind::kResume)
          return SemanticFailure{SemanticError::kUnsupportedInstruction, block.end};
        connect(Commit(end - 1), Before(graph.blocks[edge.target.value].first_instruction),
            edge.kind == FlowEdgeKind::kTaken ? ExceptionArcKind::kTaken
                                               : ExceptionArcKind::kFallthrough);
      }
    }
    result.allocated_bytes = result.nodes.capacity() * sizeof(ExceptionNode) +
        result.instructions.capacity() * sizeof(SemanticInstruction);
    result.work_units = budget.graph_work - work;
    return result;
  } catch (const WorkLimit&) {
    return SemanticFailure{SemanticError::kSsa, {}, SsaError::kWorkLimit};
  } catch (const std::bad_alloc&) { return Resource();
  } catch (const std::length_error&) { return Resource(); }
}
}  // namespace abcd::analysis
