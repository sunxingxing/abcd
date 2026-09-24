#include "abcd/analysis/exception_ssa.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::analysis {
std::variant<ExceptionSsa, RegisterUseFailure, IdfError, DominatorError, SsaError>
ExceptionSsa::Build(const ExceptionFlow& flow, std::uint64_t frame_size,
                    IdfQuota& quota, IdfBudget budget) {
  const auto consumed = quota.Consumed();
  const auto count = flow.nodes.size();
  constexpr auto kSiteBytes = sizeof(RegisterSite) + sizeof(FlowBlock);
  if (count > UINT32_MAX || count > budget.bytes / kSiteBytes)
    return SsaError::kResourceLimit;
  try {
    std::vector<RegisterSite> sites;
    NormalFlow graph;
    sites.reserve(count);
    graph.blocks.reserve(count);
    if (sites.capacity() > count || graph.blocks.capacity() > count)
      return SsaError::kResourceLimit;
    for (std::size_t i = 0; i < count; ++i) {
      if (!quota.Take(1)) return SsaError::kWorkLimit;
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= flow.instructions.size() ||
          node.successor_count > node.successors.size())
        return RegisterUseFailure{RegisterUseError::kInvalidGraph, {}};
      const auto& instruction = flow.instructions[node.instruction.value];
      RegisterEffect effect;
      switch (node.phase) {
        case ExceptionPhase::kBefore:
          effect = instruction.effects.normal_registers;
          effect.write.reset();
          break;
        case ExceptionPhase::kCommit:
          effect.write = instruction.effects.normal_registers.write;
          break;
        case ExceptionPhase::kCatch:
          effect.write = Accumulator{};
          break;
      }
      sites.push_back({instruction.pc, effect});
      FlowBlock block;
      block.begin = instruction.pc;
      block.end = instruction.pc;
      block.first_instruction = static_cast<std::uint32_t>(i);
      block.instruction_count = 1;
      block.successor_count = node.successor_count;
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        if (!quota.Take(1)) return SsaError::kWorkLimit;
        if (node.successors[e].target.value >= count)
          return RegisterUseFailure{RegisterUseError::kInvalidGraph, instruction.pc};
        // Dominance/IDF require topology and successor identity, not a second
        // exception interpretation. The owning flow retains every arc kind.
        block.successors[e] = {BlockId{node.successors[e].target.value},
            node.successors[e].kind == ExceptionArcKind::kFallthrough
                ? FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken};
      }
      graph.blocks.push_back(block);
    }
    const auto retained = sites.capacity() * sizeof(RegisterSite) +
        graph.blocks.capacity() * sizeof(FlowBlock);
    auto built = RegisterSsa::BuildSites(sites, graph, frame_size, quota,
                                        {budget.bytes - retained});
    return std::visit([&](auto&& value)
        -> std::variant<ExceptionSsa, RegisterUseFailure, IdfError, DominatorError, SsaError> {
      using T = std::remove_cvref_t<decltype(value)>;
      if constexpr (std::is_same_v<T, RegisterSsa>) {
        value.work.peak_bytes += retained;
        value.work.units = quota.Consumed() - consumed;
        return ExceptionSsa{std::move(value)};
      } else return value;
    }, std::move(built));
  } catch (const std::bad_alloc&) { return SsaError::kResourceLimit;
  } catch (const std::length_error&) { return SsaError::kResourceLimit; }
}
}  // namespace abcd::analysis
