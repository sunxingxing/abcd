#include "abcd/analysis/derived_constructor.h"

#include <new>
#include <stdexcept>
#include <vector>

#include "abcd/analysis/dominators.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
namespace {
enum class Origin : std::uint8_t {
  kBottom,
  kUndefined,
  kSuperResult,
  kOther
};
struct Node {
  Origin origin = Origin::kBottom;
};
struct Edge {
  SsaValueId source;
  SsaValueId target;
};
Origin Join(Origin left, Origin right) {
  if (left == Origin::kBottom) return right;
  if (right == Origin::kBottom || left == right) return left;
  return Origin::kOther;
}
bool CopyOpcode(abc::Opcode opcode) {
  using abc::Opcode;
  switch (opcode) {
    case Opcode::kMOV_V4_V4:
    case Opcode::kMOV_V8_V8:
    case Opcode::kMOV_V16_V16:
    case Opcode::kLDA_V8:
    case Opcode::kSTA_V8:
      return true;
    default:
      return false;
  }
}
bool DominatesInstruction(const DominatorTree& tree,
                          std::span<const BlockId> blocks,
                          std::size_t first, std::size_t later) {
  const auto source = blocks[first];
  const auto target = blocks[later];
  return source == target ? first <= later
                          : tree.Dominates(source, target);
}
}  // namespace

RangeDerivedConstructorResult ProveRangeDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget) {
  using Error = DerivedConstructorError;
  if (!frame.function || !frame.new_target || !frame.this_value ||
      frame.registers < frame.locals ||
      frame.this_value->value >= frame.registers ||
      frame.this_value->value < frame.locals)
    return Error::kFrame;
  if (body.instructions.empty() || !body.tries.empty() ||
      !body.handlers.empty())
    return Error::kControlFlow;
  std::optional<std::size_t> super_index;
  std::size_t guard_count = 0;
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto& site = body.instructions[i];
    if (site.branch_target &&
        site.branch_target->value <= site.offset.value)
      return Error::kControlFlow;
    const auto lifted = LiftSuperInstruction(site.decoded, frame.registers);
    if (std::holds_alternative<SuperRange>(lifted)) {
      if (super_index) return Error::kInstruction;
      super_index = i;
    } else if (std::holds_alternative<SuperCheck>(lifted)) {
      ++guard_count;
    } else if (!std::holds_alternative<SuperError>(lifted) ||
               std::get<SuperError>(lifted) !=
                   SuperError::kUnsupportedOpcode) {
      return Error::kInstruction;
    }
  }
  if (!super_index || guard_count == 0) return Error::kInstruction;
  try {
    const auto built = BuildNormalFlow(body, {budget.bytes});
    if (!std::holds_alternative<NormalFlow>(built))
      return Error::kControlFlow;
    const auto& graph = std::get<NormalFlow>(built);
    const auto graph_bytes = graph.blocks.capacity() * sizeof(FlowBlock);
    if (graph_bytes > budget.bytes) return Error::kResourceLimit;
    budget.bytes -= graph_bytes;
    const auto dominators = BuildNormalDominators(graph, {}, {budget.bytes});
    if (const auto* error = std::get_if<DominatorError>(&dominators))
      return *error == DominatorError::kResourceLimit
                 ? Error::kResourceLimit : Error::kControlFlow;
    const auto& tree = std::get<DominatorTree>(dominators);
    if (tree.work.allocated_bytes > budget.bytes)
      return Error::kResourceLimit;
    budget.bytes -= tree.work.allocated_bytes;
    if (body.instructions.size() > budget.bytes / sizeof(BlockId))
      return Error::kResourceLimit;
    std::vector<BlockId> blocks(body.instructions.size());
    if (blocks.capacity() > budget.bytes / sizeof(BlockId))
      return Error::kResourceLimit;
    budget.bytes -= blocks.capacity() * sizeof(BlockId);
    for (std::uint32_t block = 0; block < graph.blocks.size(); ++block) {
      const auto& region = graph.blocks[block];
      for (std::size_t i = region.first_instruction;
           i < std::size_t(region.first_instruction) +
                   region.instruction_count; ++i)
        blocks[i] = BlockId{block};
    }
    std::size_t returns = 0;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto opcode = body.instructions[i].decoded.opcode;
      if (opcode == abc::Opcode::kRETURN ||
          opcode == abc::Opcode::kRETURNUNDEFINED) {
        ++returns;
        if (!DominatesInstruction(tree, blocks, *super_index, i))
          return Error::kReturn;
      }
      if (std::holds_alternative<SuperCheck>(LiftSuperInstruction(
              body.instructions[i].decoded, frame.registers)) &&
          !DominatesInstruction(tree, blocks, *super_index, i))
        return Error::kGuard;
    }
    if (returns == 0) return Error::kReturn;
    IdfQuota quota{budget.work};
    const auto built_ssa = RegisterSsa::Build(
        body, graph, frame.registers, quota, {budget.bytes});
    if (const auto* error = std::get_if<RegisterUseFailure>(&built_ssa))
      return error->kind == RegisterUseError::kResourceLimit ||
                     error->kind == RegisterUseError::kWorkLimit
                 ? Error::kResourceLimit : Error::kInstruction;
    if (const auto* error = std::get_if<IdfError>(&built_ssa))
      return *error == IdfError::kResourceLimit ||
                     *error == IdfError::kWorkLimit
                 ? Error::kResourceLimit : Error::kControlFlow;
    if (const auto* error = std::get_if<DominatorError>(&built_ssa))
      return *error == DominatorError::kResourceLimit
                 ? Error::kResourceLimit : Error::kControlFlow;
    if (const auto* error = std::get_if<SsaError>(&built_ssa))
      return *error == SsaError::kResourceLimit ||
                     *error == SsaError::kWorkLimit
                 ? Error::kResourceLimit : Error::kControlFlow;
    const auto& ssa = std::get<RegisterSsa>(built_ssa);
    if (ssa.AllocatedBytes() > budget.bytes)
      return Error::kResourceLimit;
    budget.bytes -= ssa.AllocatedBytes();
    budget.work -= quota.Consumed();
    const auto count = ssa.Values().size();
    if (count > budget.bytes / (sizeof(Node) + sizeof(std::size_t)))
      return Error::kResourceLimit;
    std::vector<Node> nodes(count);
    std::vector<std::size_t> begin(count + 1);
    if (nodes.capacity() > budget.bytes / sizeof(Node))
      return Error::kResourceLimit;
    budget.bytes -= nodes.capacity() * sizeof(Node);
    if (begin.capacity() > budget.bytes / sizeof(std::size_t))
      return Error::kResourceLimit;
    budget.bytes -= begin.capacity() * sizeof(std::size_t);
    std::size_t edge_limit = count;
    for (std::size_t i = 0; i < count; ++i)
      if (std::holds_alternative<PhiValue>(ssa.Values()[i].origin)) {
        const auto inputs = ssa.Inputs(
            SsaValueId{static_cast<std::uint32_t>(i)});
        if (inputs.size() > SIZE_MAX - edge_limit)
          return Error::kResourceLimit;
        edge_limit += inputs.size();
      }
    if (edge_limit > budget.bytes / sizeof(Edge))
      return Error::kResourceLimit;
    std::vector<Edge> edges;
    edges.reserve(edge_limit);
    if (edges.capacity() > budget.bytes / sizeof(Edge))
      return Error::kResourceLimit;
    budget.bytes -= edges.capacity() * sizeof(Edge);
    for (std::size_t i = 0; i < count; ++i) {
      const auto& value = ssa.Values()[i];
      auto& node = nodes[i];
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
        node.origin = reg && *reg == *frame.this_value
                          ? Origin::kUndefined : Origin::kOther;
        continue;
      }
      if (std::holds_alternative<PhiValue>(value.origin)) {
        const auto inputs = ssa.Inputs(SsaValueId{static_cast<std::uint32_t>(i)});
        for (const auto& input : inputs)
          edges.push_back({input.value,
                           SsaValueId{static_cast<std::uint32_t>(i)}});
        continue;
      }
      const auto instruction =
          std::get<InstructionValue>(value.origin).instruction;
      const auto opcode = body.instructions[instruction.value].decoded.opcode;
      if (instruction.value == *super_index) {
        node.origin = Origin::kSuperResult;
      } else if (opcode == abc::Opcode::kLDUNDEFINED) {
        node.origin = Origin::kUndefined;
      } else if (CopyOpcode(opcode)) {
        const auto reads = ssa.Reads(instruction);
        if (reads.size() != 1)
          return Error::kInstruction;
        edges.push_back({reads.front().value,
                         SsaValueId{static_cast<std::uint32_t>(i)}});
      } else {
        node.origin = Origin::kOther;
      }
    }
    for (const auto edge : edges) {
      if (edge.source.value >= count || edge.target.value >= count)
        return Error::kValue;
      ++begin[edge.source.value + 1];
    }
    for (std::size_t i = 1; i < begin.size(); ++i)
      begin[i] += begin[i - 1];
    if (edges.size() > budget.bytes / sizeof(SsaValueId))
      return Error::kResourceLimit;
    std::vector<SsaValueId> targets(edges.size());
    if (targets.capacity() > budget.bytes / sizeof(SsaValueId))
      return Error::kResourceLimit;
    budget.bytes -= targets.capacity() * sizeof(SsaValueId);
    constexpr std::size_t kMaxQueuesPerNode = 2;
    if (count > budget.bytes /
                    (sizeof(std::size_t) +
                     kMaxQueuesPerNode * sizeof(SsaValueId)))
      return Error::kResourceLimit;
    std::vector<std::size_t> cursor(begin.begin(), begin.end() - 1);
    std::vector<SsaValueId> pending;
    pending.reserve(count * kMaxQueuesPerNode);
    if (cursor.capacity() > budget.bytes / sizeof(std::size_t))
      return Error::kResourceLimit;
    budget.bytes -= cursor.capacity() * sizeof(std::size_t);
    if (pending.capacity() > budget.bytes / sizeof(SsaValueId))
      return Error::kResourceLimit;
    budget.bytes -= pending.capacity() * sizeof(SsaValueId);
    for (const auto edge : edges)
      targets[cursor[edge.source.value]++] = edge.target;
    for (std::size_t i = 0; i < count; ++i)
      if (nodes[i].origin != Origin::kBottom)
        pending.push_back(SsaValueId{static_cast<std::uint32_t>(i)});
    std::uint64_t visits = 0;
    for (std::size_t head = 0; head < pending.size(); ++head) {
      const auto source = pending[head];
      for (std::size_t edge = begin[source.value];
           edge < begin[source.value + 1]; ++edge) {
        if (++visits > budget.work) return Error::kResourceLimit;
        auto& target = nodes[targets[edge].value];
        const auto joined = Join(target.origin, nodes[source.value].origin);
        if (joined == target.origin) continue;
        target.origin = joined;
        pending.push_back(targets[edge]);
      }
    }
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto checked = LiftSuperInstruction(
          body.instructions[i].decoded, frame.registers);
      const auto* guard = std::get_if<SuperCheck>(&checked);
      if (!guard) continue;
      const auto reads = ssa.Reads(InstructionId{static_cast<std::uint32_t>(i)});
      if (reads.size() != 1 || reads.front().value.value >= count)
        return Error::kGuard;
      const auto input = nodes[reads.front().value.value].origin;
      if ((guard->kind == SuperCheckKind::kRequireUninitialized &&
           input != Origin::kUndefined) ||
          (guard->kind == SuperCheckKind::kRequireInitialized &&
           input != Origin::kSuperResult))
        return Error::kGuard;
    }
    return RangeDerivedConstructor{body.instructions[*super_index].offset};
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
