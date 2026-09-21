#include "abcd/analysis/environment_flow.h"

#include <new>
#include <utility>

namespace abcd::analysis {
std::variant<EnvironmentFlow, EnvironmentFlowFailure> AnalyzeEnvironments(
    const abc::MethodBody& body, const NormalFlow& graph,
    std::uint64_t frame_size, EnvironmentBudget budget) {
  if (!body.tries.empty() || !body.handlers.empty())
    return EnvironmentFlowFailure{EnvironmentFlowError::kExceptionRegions, {}};
  if (body.instructions.size() > UINT32_MAX)
    return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
  std::size_t covered = 0;
  for (const auto& block : graph.blocks) {
    if (block.first_instruction != covered || block.instruction_count == 0 ||
        block.instruction_count > body.instructions.size() - covered)
      return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph,
                                    block.begin};
    covered += block.instruction_count;
    if (block.successor_count > block.successors.size())
      return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph,
                                    block.begin};
    for (std::size_t i = 0; i < block.successor_count; ++i) {
      const auto edge = block.successors[i];
      if (edge.target.value >= graph.blocks.size())
        return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph,
                                      block.begin};
      switch (edge.kind) {
        case FlowEdgeKind::kTaken:
        case FlowEdgeKind::kFallthrough:
          break;
        case FlowEdgeKind::kResume:
          return EnvironmentFlowFailure{EnvironmentFlowError::kResumeEdge,
                                        block.begin};
        default:
          return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph,
                                        block.begin};
      }
    }
  }
  if (graph.blocks.empty() || covered != body.instructions.size())
    return EnvironmentFlowFailure{EnvironmentFlowError::kInvalidGraph, {}};
  try {
    std::size_t spent = 0;
    std::size_t pushes = 0, accesses = 0;
    std::uint64_t visits = 0;
    for (const auto& instruction : body.instructions) {
      ++visits;
      const auto operation =
          LiftLexicalInstruction(instruction.decoded, frame_size);
      if (const auto* error = std::get_if<LexicalError>(&operation)) {
        if (*error != LexicalError::kUnsupportedOpcode)
          return EnvironmentFlowFailure{EnvironmentFlowError::kMalformedLexical,
                                        instruction.offset, *error};
      } else if (std::holds_alternative<PushEnvironment>(operation)) {
        ++pushes;
      } else if (std::holds_alternative<LexicalLoad>(operation) ||
                 std::holds_alternative<LexicalStore>(operation)) {
        ++accesses;
      }
    }
    std::vector<std::optional<EnvironmentRef>> entries, before;
    std::vector<BlockId> pending;
    std::vector<EnvironmentDefinition> definitions;
    std::vector<BoundLexicalSite> bindings;
    std::vector<EnvironmentAllocation> allocations;
    const auto reserve = [&]<class T>(std::vector<T>& storage,
                                      std::size_t count) {
      if (count > storage.max_size() ||
          count > (budget.bytes - spent) / sizeof(T))
        return false;
      storage.reserve(count);
      if (storage.capacity() > (budget.bytes - spent) / sizeof(T)) return false;
      spent += storage.capacity() * sizeof(T);
      return true;
    };
    if (!reserve(entries, graph.blocks.size()) ||
        !reserve(pending, graph.blocks.size()) ||
        !reserve(before, body.instructions.size()) ||
        !reserve(definitions, pushes) || !reserve(bindings, accesses) ||
        !reserve(allocations, pushes))
      return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
    entries.resize(graph.blocks.size());
    before.resize(body.instructions.size());
    entries[0] = CapturedDepth{0};
    pending.push_back({0});
    // Flat agreement lattice: unseen -> one environment site -> conflict.
    // A block is scheduled only on the first transition. Matching backedges
    // need no reprocessing; any later disagreement invalidates the whole
    // result.
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
      const auto id = pending[cursor];
      const auto& block = graph.blocks[id.value];
      auto current = *entries[id.value];
      for (std::size_t i = block.first_instruction;
           i < std::size_t(block.first_instruction) + block.instruction_count;
           ++i) {
        ++visits;
        const auto& instruction = body.instructions[i];
        before[i] = current;
        const auto operation =
            LiftLexicalInstruction(instruction.decoded, frame_size);
        if (const auto* push = std::get_if<PushEnvironment>(&operation)) {
          const EnvironmentId allocated{
              static_cast<std::uint32_t>(definitions.size())};
          definitions.push_back({current, *push});
          allocations.push_back({static_cast<std::uint32_t>(i), allocated});
          current = allocated;
        } else if (std::holds_alternative<PopEnvironment>(operation)) {
          if (const auto* local = std::get_if<EnvironmentId>(&current)) {
            current = definitions[local->value].parent;
          } else {
            const auto capture = std::get<CapturedDepth>(current);
            if (capture.value == UINT32_MAX)
              return EnvironmentFlowFailure{EnvironmentFlowError::kBinding,
                                            instruction.offset,
                                            {},
                                            EnvironmentError::kDepthRange};
            current = CapturedDepth{capture.value + 1};
          }
        }
      }
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        const auto target = block.successors[i].target;
        auto& entry = entries[target.value];
        // A common parent after branch-local pushes/pops can join. Different
        // allocation sites cannot: choosing either would silently bind
        // the other branch's lexical accesses and closures to the wrong scope.
        if (entry && *entry != current)
          return EnvironmentFlowFailure{EnvironmentFlowError::kAmbiguousJoin,
                                        graph.blocks[target.value].begin};
        if (!entry) {
          entry = current;
          pending.push_back(target);
        }
      }
    }
    auto indexed = EnvironmentIndex::Build(definitions, {budget.bytes - spent});
    if (const auto* error = std::get_if<EnvironmentError>(&indexed))
      return EnvironmentFlowFailure{
          EnvironmentFlowError::kBinding, {}, {}, *error};
    auto& environments = std::get<EnvironmentIndex>(indexed);
    spent += environments.AllocatedBytes();
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      ++visits;
      if (!before[i]) continue;
      const auto& instruction = body.instructions[i];
      const auto operation =
          LiftLexicalInstruction(instruction.decoded, frame_size);
      const LexicalAddress* address = nullptr;
      if (const auto* load = std::get_if<LexicalLoad>(&operation))
        address = &load->address;
      if (const auto* store = std::get_if<LexicalStore>(&operation))
        address = &store->address;
      if (!address) continue;
      const auto bound = environments.Resolve(*before[i], *address);
      if (const auto* error = std::get_if<EnvironmentError>(&bound))
        return EnvironmentFlowFailure{
            EnvironmentFlowError::kBinding, instruction.offset, {}, *error};
      bindings.push_back(
          {static_cast<std::uint32_t>(i), std::get<LexicalBinding>(bound)});
    }
    return EnvironmentFlow{std::move(environments),
                           std::move(before),
                           std::move(bindings),
                           std::move(allocations),
                           spent,
                           visits};
  } catch (const std::bad_alloc&) {
    return EnvironmentFlowFailure{EnvironmentFlowError::kResourceLimit, {}};
  }
}
}  // namespace abcd::analysis
