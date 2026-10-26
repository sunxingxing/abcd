#include "abcd/analysis/derived_constructor.h"

#include <new>
#include <stdexcept>
#include <vector>

#include "abcd/analysis/dominators.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/local_ssa.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
RangeDerivedConstructorResult ProveRangeConstructorProtocol(
    const abc::MethodBody&, const abc::FrameLayout&, DerivedConstructorBudget,
    abc::ProfileId);
namespace {
enum class Origin : std::uint8_t {
  kBottom,
  kUndefined,
  kSuperResult,
  kCurrentConstructor,
  kOther
};
struct Node {
  Origin origin = Origin::kBottom;
};
struct OriginId {
  std::uint32_t value;
};
struct Edge {
  OriginId source;
  OriginId target;
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
std::optional<DerivedConstructorError> ProveSingleInvocation(
    const NormalFlow& graph, BlockId call, DerivedConstructorBudget& budget) {
  using Error = DerivedConstructorError;
  // Initialization represents one dynamic super invocation. A backward PC is
  // not repetition: follow actual successors once and reject only a cycle
  // through the call block. Independent loops and layout permutations are
  // transparent. Normal CFG cannot prove missing exceptional/resume paths.
  if (call.value >= graph.blocks.size()) return Error::kControlFlow;
  const auto count = graph.blocks.size();
  if (count > budget.bytes / sizeof(std::uint8_t)) return Error::kResourceLimit;
  std::vector<std::uint8_t> seen(count);
  if (seen.capacity() > budget.bytes / sizeof(std::uint8_t))
    return Error::kResourceLimit;
  budget.bytes -= seen.capacity() * sizeof(std::uint8_t);
  if (count > budget.bytes / sizeof(BlockId)) return Error::kResourceLimit;
  std::vector<BlockId> pending;
  pending.reserve(count);
  if (pending.capacity() > budget.bytes / sizeof(BlockId))
    return Error::kResourceLimit;
  budget.bytes -= pending.capacity() * sizeof(BlockId);
  if (count > budget.work) return Error::kResourceLimit;
  budget.work -= count;
  seen[call.value] = 1;
  pending.push_back(call);
  for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
    const auto& block = graph.blocks[pending[cursor].value];
    for (std::size_t e = 0; e < block.successor_count; ++e) {
      if (budget.work == 0) return Error::kResourceLimit;
      --budget.work;
      const auto edge = block.successors[e];
      if (edge.kind != FlowEdgeKind::kTaken &&
          edge.kind != FlowEdgeKind::kFallthrough)
        return Error::kControlFlow;
      if (edge.target.value >= count || edge.target == call)
        return Error::kControlFlow;
      if (!seen[edge.target.value]) {
        seen[edge.target.value] = 1;
        pending.push_back(edge.target);
      }
    }
  }
  // Scratch is dead on return; its peak admission remains checked above.
  budget.bytes += seen.capacity() * sizeof(std::uint8_t) +
                  pending.capacity() * sizeof(BlockId);
  return {};
}
enum class CallForm { kRange, kSpread };
using InitializationResult = std::variant<abc::CodeOffset, DerivedConstructorError>;
InitializationResult ProveInitialization(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget, abc::ProfileId profile,
    std::span<const CapturePreservation> preserved, CallForm form) {
  using Error = DerivedConstructorError;
  if (!frame.function || !frame.new_target || !frame.this_value ||
      frame.registers < frame.locals ||
      frame.this_value->value >= frame.registers ||
      frame.this_value->value < frame.locals)
    return Error::kFrame;
  if (form == CallForm::kSpread &&
      (frame.function->value >= frame.registers ||
       frame.function->value < frame.locals ||
       frame.new_target->value >= frame.registers ||
       frame.new_target->value < frame.locals ||
       frame.function == frame.this_value ||
       frame.function == frame.new_target || frame.new_target == frame.this_value))
    return Error::kFrame;
  if (body.instructions.empty() || !body.tries.empty() ||
      !body.handlers.empty())
    return Error::kControlFlow;
  std::optional<std::size_t> super_index;
  bool lexical_loads = false;
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto& site = body.instructions[i];
    lexical_loads |= std::holds_alternative<LexicalLoad>(
        LiftLexicalInstruction(site.decoded, frame.registers));
    const auto lifted = LiftSuperInstruction(site.decoded, frame.registers);
    if ((form == CallForm::kRange && std::holds_alternative<SuperRange>(lifted)) ||
        (form == CallForm::kSpread && std::holds_alternative<SuperSpread>(lifted))) {
      if (super_index) return Error::kInstruction;
      super_index = i;
    } else if (!std::holds_alternative<SuperCheck>(lifted) &&
               (!std::holds_alternative<SuperError>(lifted) ||
                std::get<SuperError>(lifted) !=
                    SuperError::kUnsupportedOpcode)) {
      return Error::kInstruction;
    }
  }
  if (!super_index) return Error::kInstruction;
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
    if (const auto error = ProveSingleInvocation(graph, blocks[*super_index], budget))
      return *error;
    std::size_t returns = 0;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto opcode = body.instructions[i].decoded.opcode;
      if (opcode == abc::Opcode::kRETURN ||
          opcode == abc::Opcode::kRETURNUNDEFINED) {
        ++returns;
        if (!DominatesInstruction(tree, blocks, *super_index, i))
          return Error::kReturn;
      }
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
    std::optional<LocalSsa> locals;
    if (lexical_loads) {
      auto built_locals = BuildLocalSsa(body, graph, ssa, frame.registers, profile,
                                       {budget.bytes}, budget.work, preserved);
      if (auto* local = std::get_if<LocalSsa>(&built_locals)) {
        if (local->AllocatedBytes() > budget.bytes ||
            local->work_units > budget.work)
          return Error::kResourceLimit;
        budget.bytes -= local->AllocatedBytes();
        budget.work -= local->work_units;
        locals.emplace(std::move(*local));
      } else if (std::get<LocalSsaError>(built_locals) == LocalSsaError::kResourceLimit) {
        return Error::kResourceLimit;
      }
      // Unsupported cell effects retain the original register-only proof;
      // they never manufacture an origin for a lexical load.
    }
    const auto register_count = ssa.Values().size();
    const auto local_count = locals ? locals->values.Values().size() : 0;
    if (register_count > UINT32_MAX || local_count > UINT32_MAX - register_count)
      return Error::kResourceLimit;
    const auto count = register_count + local_count;
    const auto local_id = [&](SsaValueId value) {
      return OriginId{static_cast<std::uint32_t>(register_count + value.value)};
    };
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
    for (std::size_t i = 0; i < register_count; ++i)
      if (std::holds_alternative<PhiValue>(ssa.Values()[i].origin)) {
        const auto inputs = ssa.Inputs(
            SsaValueId{static_cast<std::uint32_t>(i)});
        if (inputs.size() > SIZE_MAX - edge_limit)
          return Error::kResourceLimit;
        edge_limit += inputs.size();
      }
    for (std::size_t i = 0; i < local_count; ++i)
      if (std::holds_alternative<PhiValue>(locals->values.Values()[i].origin)) {
        const auto inputs = locals->values.Inputs(
            SsaValueId{static_cast<std::uint32_t>(i)});
        if (inputs.size() > SIZE_MAX - edge_limit) return Error::kResourceLimit;
        edge_limit += inputs.size();
      }
    if (edge_limit > budget.bytes / sizeof(Edge))
      return Error::kResourceLimit;
    std::vector<Edge> edges;
    edges.reserve(edge_limit);
    if (edges.capacity() > budget.bytes / sizeof(Edge))
      return Error::kResourceLimit;
    budget.bytes -= edges.capacity() * sizeof(Edge);
    for (std::size_t i = 0; i < register_count; ++i) {
      const auto& value = ssa.Values()[i];
      auto& node = nodes[i];
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
        node.origin = reg && *reg == *frame.this_value ? Origin::kUndefined
                    : reg && *reg == *frame.function ? Origin::kCurrentConstructor
                                                    : Origin::kOther;
        continue;
      }
      if (std::holds_alternative<PhiValue>(value.origin)) {
        const auto inputs = ssa.Inputs(SsaValueId{static_cast<std::uint32_t>(i)});
        for (const auto& input : inputs)
          edges.push_back({OriginId{input.value.value},
                           OriginId{static_cast<std::uint32_t>(i)}});
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
        edges.push_back({OriginId{reads.front().value.value},
                         OriginId{static_cast<std::uint32_t>(i)}});
      } else if (locals &&
                 locals->PreservesRead(instruction) &&
                 std::holds_alternative<LexicalLoad>(LiftLexicalInstruction(
                     body.instructions[instruction.value].decoded, frame.registers))) {
        const auto reads = locals->values.Reads(instruction);
        if (reads.size() == 1)
          edges.push_back({local_id(reads.front().value),
                           OriginId{static_cast<std::uint32_t>(i)}});
        else
          node.origin = Origin::kOther;
      } else {
        node.origin = Origin::kOther;
      }
    }
    for (std::size_t i = 0; i < local_count; ++i) {
      const auto& value = locals->values.Values()[i];
      const auto target = local_id(SsaValueId{static_cast<std::uint32_t>(i)});
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        nodes[target.value].origin = Origin::kOther;
      } else if (std::holds_alternative<PhiValue>(value.origin)) {
        for (const auto& input : locals->values.Inputs(
                 SsaValueId{static_cast<std::uint32_t>(i)}))
          edges.push_back({local_id(input.value), target});
      } else {
        const auto site = std::get<InstructionValue>(value.origin).instruction;
        const auto reads = ssa.Reads(site);
        if (reads.size() != 1) return Error::kInstruction;
        edges.push_back({OriginId{reads.front().value.value}, target});
      }
    }
    for (const auto edge : edges) {
      if (edge.source.value >= count || edge.target.value >= count)
        return Error::kValue;
      ++begin[edge.source.value + 1];
    }
    for (std::size_t i = 1; i < begin.size(); ++i)
      begin[i] += begin[i - 1];
    if (edges.size() > budget.bytes / sizeof(OriginId))
      return Error::kResourceLimit;
    std::vector<OriginId> targets(edges.size());
    if (targets.capacity() > budget.bytes / sizeof(OriginId))
      return Error::kResourceLimit;
    budget.bytes -= targets.capacity() * sizeof(OriginId);
    constexpr std::size_t kMaxQueuesPerNode = 2;
    if (count > budget.bytes /
                    (sizeof(std::size_t) +
                     kMaxQueuesPerNode * sizeof(OriginId)))
      return Error::kResourceLimit;
    std::vector<std::size_t> cursor(begin.begin(), begin.end() - 1);
    std::vector<OriginId> pending;
    pending.reserve(count * kMaxQueuesPerNode);
    if (cursor.capacity() > budget.bytes / sizeof(std::size_t))
      return Error::kResourceLimit;
    budget.bytes -= cursor.capacity() * sizeof(std::size_t);
    if (pending.capacity() > budget.bytes / sizeof(OriginId))
      return Error::kResourceLimit;
    budget.bytes -= pending.capacity() * sizeof(OriginId);
    for (const auto edge : edges)
      targets[cursor[edge.source.value]++] = edge.target;
    for (std::size_t i = 0; i < count; ++i)
      if (nodes[i].origin != Origin::kBottom)
        pending.push_back(OriginId{static_cast<std::uint32_t>(i)});
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
    // Spread's acc is an explicit current-constructor operand. Prove its
    // original SSA identity through copies/phis rather than assuming any
    // callable invokes this class's parent. Argument expansion is separate.
    if (form == CallForm::kSpread) {
      const auto reads = ssa.Reads(InstructionId{static_cast<std::uint32_t>(*super_index)});
      if (reads.size() != 2 || reads[0].ordinal != 0 ||
          reads[0].value.value >= register_count ||
          !std::holds_alternative<Accumulator>(ssa.Values()[reads[0].value.value].slot) ||
          nodes[reads[0].value.value].origin != Origin::kCurrentConstructor)
        return Error::kValue;
    }
    // Each guard is discharged by its actual SSA origin, including guards
    // before super and runtime no-ops. Presence or placement is not a protocol.
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
    return body.instructions[*super_index].offset;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace
RangeDerivedConstructorResult ProveRangeDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget, abc::ProfileId profile,
    std::span<const CapturePreservation> preserved) {
  const auto result = ProveInitialization(body, frame, budget, profile, preserved, CallForm::kRange);
  if (const auto* error = std::get_if<DerivedConstructorError>(&result)) {
    if (*error == DerivedConstructorError::kResourceLimit ||
        *error == DerivedConstructorError::kFrame)
      return *error;
    auto general = ProveRangeConstructorProtocol(body, frame, budget, profile);
    if (const auto* refused = std::get_if<DerivedConstructorError>(&general);
        refused && *refused != DerivedConstructorError::kResourceLimit)
      return *error;
    return general;
  }
  const auto pc = std::get<abc::CodeOffset>(result);
  RangeDerivedConstructor proof;
  std::size_t guards = 0, returns = 0;
  for (const auto& site : body.instructions) {
    guards += std::holds_alternative<SuperCheck>(
        LiftSuperInstruction(site.decoded, frame.registers));
    returns += site.decoded.opcode == abc::Opcode::kRETURN ||
               site.decoded.opcode == abc::Opcode::kRETURNUNDEFINED;
  }
  if (budget.bytes < sizeof(DerivedSuperSite) ||
      guards >
          (budget.bytes - sizeof(DerivedSuperSite)) / sizeof(DerivedCoverage))
    return DerivedConstructorError::kResourceLimit;
  budget.bytes -= sizeof(DerivedSuperSite) + guards * sizeof(DerivedCoverage);
  if (returns > budget.bytes / sizeof(DerivedReturnSite))
    return DerivedConstructorError::kResourceLimit;
  try {
    proof.sites.reserve(1);
    proof.coverage.reserve(guards);
    proof.returns.reserve(returns);
    for (std::uint32_t i = 0; i < body.instructions.size(); ++i) {
      const auto& site = body.instructions[i];
      if (site.offset == pc) proof.sites.push_back({{i}, pc});
      if (std::holds_alternative<SuperCheck>(
              LiftSuperInstruction(site.decoded, frame.registers)))
        proof.coverage.push_back({{i}, DerivedCheckAction::kNoop, {}});
      if (site.decoded.opcode == abc::Opcode::kRETURN ||
          site.decoded.opcode == abc::Opcode::kRETURNUNDEFINED)
        proof.returns.push_back({{i}, ReceiverState::kInitialized});
    }
    proof.allocated_bytes =
        proof.sites.capacity() * sizeof(DerivedSuperSite) +
        proof.returns.capacity() * sizeof(DerivedReturnSite) +
        proof.coverage.capacity() * sizeof(DerivedCoverage);
    return proof;
  } catch (const std::bad_alloc&) {
    return DerivedConstructorError::kResourceLimit;
  } catch (const std::length_error&) {
    return DerivedConstructorError::kResourceLimit;
  }
}
SpreadDerivedConstructorResult ProveSpreadDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget, abc::ProfileId profile,
    std::span<const CapturePreservation> preserved) {
  const auto result = ProveInitialization(body, frame, budget, profile, preserved, CallForm::kSpread);
  if (const auto* error = std::get_if<DerivedConstructorError>(&result)) return *error;
  return SpreadDerivedConstructor{std::get<abc::CodeOffset>(result)};
}
}  // namespace abcd::analysis
