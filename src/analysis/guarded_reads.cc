#include "abcd/analysis/guarded_reads.h"

#include <algorithm>
#include <memory_resource>
#include <stdexcept>

#include "abcd/analysis/hole_guard.h"
#include "idf_scratch.h"

namespace abcd::analysis {
namespace {
struct GuardUndo {
  SsaValueId value;
  std::optional<InstructionId> previous;
};
struct GuardFrame {
  std::uint32_t end;
  std::size_t undo_begin;
};
bool ReadLess(const SsaRead& left, const SsaRead& right) {
  return left.instruction != right.instruction
             ? left.instruction < right.instruction
             : left.ordinal < right.ordinal;
}
ValueKinds WithoutHole(ValueKinds kinds) {
  ValueKinds result;
  if (kinds.Contains(VmValueKind::kLanguage))
    result = result.Join(ValueKinds::Language());
  if (kinds.Contains(VmValueKind::kEnvironment))
    result = result.Join(ValueKinds::Environment());
  if (kinds.Contains(VmValueKind::kOtherInternal))
    result = result.Join(ValueKinds::OtherInternal());
  return result;
}
}  // namespace

std::optional<GuardedRead> GuardedReads::At(InstructionId instruction,
                                            std::uint32_t ordinal) const {
  const SsaRead key{instruction, ordinal, {}};
  const auto reads = Reads();
  const auto found =
      std::lower_bound(reads.begin(), reads.end(), key,
                       [](const GuardedRead& item, const SsaRead& query) {
                         return ReadLess(item.read, query);
                       });
  if (found == reads.end() || ReadLess(key, found->read)) return {};
  return *found;
}

std::variant<GuardedReads, GuardReadError> GuardedReads::Build(
    const abc::MethodBody& body, const NormalFlow& graph,
    const RegisterSsa& ssa, const RegisterValueFlow& flow,
    std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget) {
  using Error = GuardReadError;
  if (!body.tries.empty() || !body.handlers.empty())
    return Error::kExceptionRegions;
  if (body.instructions.size() > UINT32_MAX) return Error::kResourceLimit;
  if (flow.Facts().size() != ssa.Values().size()) return Error::kInvalidSsa;
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t units = 1) {
      if (!quota.Take(units)) throw detail::IdfWorkLimit{};
    };
    std::size_t covered = 0;
    for (const auto& block : graph.blocks) {
      charge();
      if (block.first_instruction != covered || block.instruction_count == 0 ||
          block.instruction_count > body.instructions.size() - covered ||
          block.successor_count > block.successors.size())
        return Error::kInvalidGraph;
      covered += block.instruction_count;
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        charge();
        const auto edge = block.successors[i];
        if (edge.target.value >= graph.blocks.size())
          return Error::kInvalidGraph;
        switch (edge.kind) {
          case FlowEdgeKind::kTaken:
          case FlowEdgeKind::kFallthrough:
            break;
          case FlowEdgeKind::kResume:
            return Error::kResumeEdge;
          default:
            return Error::kInvalidGraph;
        }
      }
    }
    if (graph.blocks.empty() || covered != body.instructions.size())
      return Error::kInvalidGraph;
    detail::IdfScratch memory{budget.bytes};
    charge(graph.blocks.size());
    auto built = BuildNormalDominators(graph, {}, {memory.Available()});
    if (const auto* error = std::get_if<DominatorError>(&built))
      return *error == DominatorError::kResourceLimit ? Error::kResourceLimit
                                                      : Error::kInvalidGraph;
    const auto& tree = std::get<DominatorTree>(built);
    memory.Observe(tree.work.allocated_bytes);
    memory.Retain(tree.nodes.capacity() * sizeof(DominatorNode));
    charge(ssa.Values().size());
    std::pmr::vector<std::optional<InstructionId>> active(ssa.Values().size(),
                                                          {}, &memory);
    std::pmr::vector<BlockId> preorder(tree.nodes.size(), {}, &memory);
    std::pmr::vector<GuardUndo> undo{&memory};
    std::pmr::vector<GuardFrame> frames{&memory};
    std::pmr::vector<GuardedRead> reads{&memory};
    std::size_t reachable = 0;
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
      charge();
      if (tree.nodes[i].begin == 0) continue;
      preorder[tree.nodes[i].begin - 1] = {static_cast<std::uint32_t>(i)};
      ++reachable;
    }
    GuardedReads result;
    // Undo only actual guards when leaving a dominator subtree. Never copy a
    // value-sized state per block or scan the CFG separately for each value.
    for (std::size_t order = 0; order < reachable; ++order) {
      charge();
      const auto block_id = preorder[order];
      const auto node = tree.nodes[block_id.value];
      while (!frames.empty() && node.begin >= frames.back().end) {
        charge();
        while (undo.size() > frames.back().undo_begin) {
          charge();
          const auto restored = undo.back();
          active[restored.value.value] = restored.previous;
          undo.pop_back();
          ++result.work.restores;
        }
        frames.pop_back();
      }
      frames.push_back({node.end, undo.size()});
      const auto& block = graph.blocks[block_id.value];
      for (std::size_t pc = block.first_instruction;
           pc < std::size_t(block.first_instruction) + block.instruction_count;
           ++pc) {
        charge();
        ++result.work.instructions;
        const InstructionId instruction{static_cast<std::uint32_t>(pc)};
        const auto inputs = ssa.Reads(instruction);
        for (const auto& input : inputs) {
          charge();
          const auto kinds = flow.At(input.value);
          if (!kinds) return Error::kInvalidSsa;
          const auto guard = active[input.value.value];
          reads.push_back({input, guard ? WithoutHole(*kinds) : *kinds, guard});
        }
        const auto lifted =
            LiftHoleInstruction(body.instructions[pc].decoded, frame_size);
        if (const auto* error = std::get_if<HoleError>(&lifted)) {
          if (*error != HoleError::kUnsupportedOpcode)
            return Error::kInstruction;
        } else if (std::holds_alternative<HoleGuard>(lifted)) {
          // RegisterEffect orders the tested value first, diagnostic name
          // second. The name read is conditional; do not refine either read
          // using THIS guard, nor infer anything about the name's value.
          if (inputs.empty() || inputs.front().ordinal != 0)
            return Error::kInvalidSsa;
          const auto value = inputs.front().value;
          undo.push_back({value, active[value.value]});
          active[value.value] = instruction;
          ++result.work.guards;
        }
      }
    }
    std::sort(reads.begin(), reads.end(), [&](const auto& a, const auto& b) {
      charge();
      return ReadLess(a.read, b.read);
    });
    charge(reads.size());
    if (reads.size() > memory.Available() / sizeof(GuardedRead))
      throw std::bad_alloc{};
    if (!reads.empty()) {
      result.reads_ = std::make_unique<GuardedRead[]>(reads.size());
      std::copy(reads.begin(), reads.end(), result.reads_.get());
    }
    result.count_ = reads.size();
    memory.Observe(result.AllocatedBytes());
    result.work.peak_bytes = memory.Peak();
    result.work.units = quota.Consumed() - consumed;
    return result;
  } catch (const detail::IdfWorkLimit&) {
    return Error::kWorkLimit;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
