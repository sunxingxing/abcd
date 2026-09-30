#include "abcd/analysis/exception_environment.h"

#include <new>
#include <stdexcept>

namespace abcd::analysis {
namespace {
bool HasEffect(LanguageEffect effects, LanguageEffect flag) {
  return (static_cast<std::uint16_t>(effects) &
          static_cast<std::uint16_t>(flag)) != 0;
}
}

std::variant<EnvironmentFlow, EnvironmentFlowFailure>
AnalyzeExceptionEnvironments(const ExceptionFlow& flow, EnvironmentBudget budget) {
  const auto count = flow.nodes.size();
  constexpr auto kSiteBytes = sizeof(EnvironmentSite) + sizeof(FlowBlock);
  if (count > UINT32_MAX || count > budget.bytes / kSiteBytes)
    return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
  try {
    std::vector<EnvironmentSite> sites;
    NormalFlow graph;
    sites.reserve(count);
    graph.blocks.reserve(count);
    if (sites.capacity() > count || graph.blocks.capacity() > count)
      return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
    for (std::size_t i = 0; i < count; ++i) {
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= flow.instructions.size() ||
          node.successor_count > node.successors.size())
        return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph, {}};
      const auto& instruction = flow.instructions[node.instruction.value];
      const auto& effects = instruction.effects;
      const auto* push = std::get_if<PushEnvironment>(&instruction.operation);
      const bool pop = std::holds_alternative<PopEnvironment>(instruction.operation);
      const bool native_suspension = std::holds_alternative<AwaitValue>(instruction.operation) ||
          std::holds_alternative<YieldValue>(instruction.operation);
      // Pinned interpreter EXCEPTION retains the selected frame's env. NEWLEXENV
      // installs it only after abrupt-completion checks; POPLEXENV is normal-only.
      // v7 SaveFrameToContext saves the lexenv; GeneratorReEnter restores it.
      // Only owner-normalized await/yield preserves this identity on resume or
      // injected throw. This says nothing about mutation of captured cell values.
      if (HasEffect(effects.before_throw, LanguageEffect::kEnvironmentWrite) ||
          HasEffect(effects.normal, LanguageEffect::kEnvironmentWrite) != (push || pop) ||
          (!native_suspension &&
           (HasEffect(effects.normal, LanguageEffect::kSuspension) ||
            HasEffect(effects.before_throw, LanguageEffect::kSuspension))))
        return EnvironmentFlowFailure{EnvironmentFlowError::kUnsupportedEffect, instruction.pc};
      EnvironmentOperation operation;
      switch (node.phase) {
        case ExceptionPhase::kBefore:
          if (const auto* load = std::get_if<LexicalLoad>(&instruction.operation))
            operation = *load;
          else if (const auto* store = std::get_if<LexicalStore>(&instruction.operation))
            operation = *store;
          break;
        case ExceptionPhase::kCommit:
          if (push) operation = *push;
          else if (pop) operation = PopEnvironment{};
          break;
        case ExceptionPhase::kCatch:
          break;
      }
      sites.push_back({instruction.pc, operation});
      FlowBlock block;
      block.begin = instruction.pc;
      block.end = instruction.pc;
      block.first_instruction = static_cast<std::uint32_t>(i);
      block.instruction_count = 1;
      block.successor_count = node.successor_count;
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        if (node.successors[e].target.value >= count)
          return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph, instruction.pc};
        block.successors[e] = {BlockId{node.successors[e].target.value},
            node.successors[e].kind == ExceptionArcKind::kFallthrough
                ? FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken};
      }
      graph.blocks.push_back(block);
    }
    const auto retained = sites.capacity() * sizeof(EnvironmentSite) +
        graph.blocks.capacity() * sizeof(FlowBlock);
    auto result = AnalyzeEnvironmentSites(sites, graph, {budget.bytes - retained});
    if (auto* environments = std::get_if<EnvironmentFlow>(&result)) {
      environments->allocated_bytes += retained;
      environments->instruction_visits += sites.size();
    }
    return result;
  } catch (const std::bad_alloc&) {
    return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
  } catch (const std::length_error&) {
    return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
  }
}
}  // namespace abcd::analysis
