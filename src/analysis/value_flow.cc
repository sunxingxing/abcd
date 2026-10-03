#include "value_flow.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/analysis/dominators.h"

namespace abcd::analysis {
namespace {
using Error = ValueAvailabilityError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
class FlowQuota {
 public:
  explicit FlowQuota(SemanticBudget budget) : remaining_(budget) {}
  void Work(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <typename T> void Reserve(std::vector<T>& items, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T));
    items.reserve(count);
    if (items.capacity() > count) Admit(items.capacity() - count, sizeof(T));
  }
  SemanticBudget Remaining() const { return remaining_; }
 private:
  SemanticBudget remaining_;
};
struct PhiKey {
  ValueId phi;
  std::optional<EdgeId> edge;  // Absence is MethodEntry, not a fabricated edge.
  auto operator<=>(const PhiKey&) const = default;
};

NormalFlow ReadFlow(const ValueProgram& graph, FlowQuota& quota,
                    std::vector<std::size_t>& predecessors) {
  NormalFlow flow;
  Require(!graph.blocks.empty(), Error::kInvalidGraph);
  const bool phased = !graph.phases.empty();
  Require(!phased || graph.phases.front().phase == ExceptionPhase::kBefore,
          Error::kInvalidGraph);
  quota.Reserve(flow.blocks, graph.blocks.size());
  quota.Reserve(predecessors, graph.blocks.size());
  quota.Work(graph.blocks.size());
  predecessors.resize(graph.blocks.size());
  std::size_t next_op = 0;
  std::size_t next_edge = 0;
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Work();
    const auto& block = graph.blocks[b];
    Require(block.first_op.value == next_op &&
        block.op_count <= graph.ops.size() - next_op &&
        block.first_edge.value == next_edge &&
        block.edge_count <= graph.edges.size() - next_edge &&
        block.edge_count <= kMaxNormalSuccessors, Error::kInvalidGraph);
    FlowBlock adapted;
    std::optional<OpId> last;
    const ValuePhaseSite* phase = phased ? &graph.phases[b] : nullptr;
    if (phase) {
      quota.Work();
      switch (phase->phase) {
        case ExceptionPhase::kBefore:
          Require(block.op_count == 1 && phase->operation == block.first_op,
                  Error::kInvalidGraph);
          break;
        case ExceptionPhase::kCommit:
          Require(block.op_count == 0 && phase->operation &&
              phase->operation->value < graph.ops.size(), Error::kInvalidGraph);
          break;
        case ExceptionPhase::kCatch:
          Require(block.op_count == 0 && !phase->operation, Error::kInvalidGraph);
          break;
        default: throw Error::kInvalidGraph;
      }
      if (phase->operation) {
        const auto& op = graph.ops[phase->operation->value];
        Require(op.life == ValueLife::kLive && op.origin.value < graph.origins.size() &&
            graph.origins[op.origin.value].site == phase->instruction &&
            op.block.value < graph.phases.size(), Error::kInvalidGraph);
        const auto& before = graph.phases[op.block.value];
        Require(before.phase == ExceptionPhase::kBefore &&
            before.operation == phase->operation, Error::kInvalidGraph);
      }
    }
    for (std::uint32_t order = 0; order < block.op_count; ++order) {
      quota.Work();
      const auto index = next_op + order;
      const auto& op = graph.ops[index];
      Require(op.block.value == b && op.order == order, Error::kInvalidGraph);
      if (op.life != ValueLife::kLive) continue;
      if (std::holds_alternative<ValueThrow>(op.operation))
        Require(op.exceptional == ExceptionalCompletion::kAlwaysThrow && !op.result &&
            op.use_count == 1, Error::kInvalidTerminator);
      if (last) {
        const auto& prior = graph.ops[last->value];
        Require(!std::holds_alternative<ValueReturn>(prior.operation) &&
                !std::holds_alternative<ValueJump>(prior.operation) &&
                !std::holds_alternative<ValueBranch>(prior.operation) &&
                prior.exceptional != ExceptionalCompletion::kAlwaysThrow,
                Error::kInvalidTerminator);
      }
      last = OpId{static_cast<std::uint32_t>(index)};
    }
    next_op += block.op_count;
    for (std::uint8_t e = 0; e < block.edge_count; ++e) {
      quota.Work();
      const auto& edge = graph.edges[next_edge++];
      Require(edge.source.value == b && edge.successor == e &&
              edge.target.value < graph.blocks.size(), Error::kInvalidGraph);
      Require(edge.kind != FlowEdgeKind::kResume, Error::kResumeFlow);
      Require(edge.kind == FlowEdgeKind::kTaken || edge.kind == FlowEdgeKind::kFallthrough ||
              (phased && edge.kind == FlowEdgeKind::kException),
              Error::kInvalidGraph);
      // Dominance consumes topology only. Keep exception labels in ValueProgram;
      // this private adapter does not authorize any normal-flow transformation.
      adapted.successors[adapted.successor_count++] = {{edge.target.value},
          edge.kind == FlowEdgeKind::kException ? FlowEdgeKind::kTaken : edge.kind};
      ++predecessors[edge.target.value];
    }
    if (phase && phase->phase == ExceptionPhase::kBefore) {
      const auto& op = graph.ops[phase->operation->value];
      if (op.exceptional == ExceptionalCompletion::kAlwaysThrow) {
        Require(std::holds_alternative<ValueThrow>(op.operation) && block.edge_count <= 1,
            Error::kInvalidTerminator);
        if (block.edge_count != 0) {
          const auto& edge = graph.edges[block.first_edge.value];
          Require(edge.kind == FlowEdgeKind::kException &&
              graph.phases[edge.target.value].phase == ExceptionPhase::kCatch, Error::kInvalidGraph);
        }
        flow.blocks.push_back(adapted);
        continue;
      }
      Require(block.edge_count >= 1, Error::kInvalidTerminator);
      const auto& normal = graph.edges[block.first_edge.value];
      const auto& commit = graph.phases[normal.target.value];
      Require(normal.kind == FlowEdgeKind::kFallthrough &&
          commit.phase == ExceptionPhase::kCommit && commit.operation == phase->operation,
          Error::kInvalidGraph);
      if (op.result) {
        const auto& result = std::get<OpResult>(graph.values[op.result->value].origin);
        Require(result.commit == normal.target, Error::kInvalidGraph);
      }
      if (block.edge_count == kMaxNormalSuccessors) {
        const auto& caught = graph.edges[block.first_edge.value + 1];
        Require(op.exceptional == ExceptionalCompletion::kMayThrow &&
            caught.kind == FlowEdgeKind::kException &&
            graph.phases[caught.target.value].phase == ExceptionPhase::kCatch,
            Error::kInvalidGraph);
      }
      flow.blocks.push_back(adapted);
      continue;
    }
    if (phase) {
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        quota.Work();
        const auto& edge = graph.edges[block.first_edge.value + e];
        Require(edge.kind != FlowEdgeKind::kException &&
            graph.phases[edge.target.value].phase == ExceptionPhase::kBefore,
            Error::kInvalidGraph);
        if (phase->phase == ExceptionPhase::kCatch)
          Require(phase->instruction == graph.phases[edge.target.value].instruction,
                  Error::kInvalidGraph);
      }
      if (phase->phase == ExceptionPhase::kCommit) last = phase->operation;
    }
    const auto* terminal = last ? &graph.ops[last->value] : nullptr;
    const bool returns = terminal && std::holds_alternative<ValueReturn>(terminal->operation);
    const bool jumps = terminal && std::holds_alternative<ValueJump>(terminal->operation);
    const bool branches = terminal && std::holds_alternative<ValueBranch>(terminal->operation);
    const bool throws = terminal && terminal->exceptional == ExceptionalCompletion::kAlwaysThrow;
    if (returns || throws) {
      Require(block.edge_count == 0, Error::kInvalidTerminator);
    } else if (branches) {
      Require(block.edge_count == kMaxNormalSuccessors &&
          adapted.successors[0].kind == FlowEdgeKind::kTaken &&
          adapted.successors[1].kind == FlowEdgeKind::kFallthrough,
          Error::kInvalidTerminator);
    } else {
      Require(block.edge_count == 1 && adapted.successors[0].kind ==
          (jumps ? FlowEdgeKind::kTaken : FlowEdgeKind::kFallthrough),
          Error::kInvalidTerminator);
    }
    flow.blocks.push_back(adapted);
  }
  Require(next_op == graph.ops.size() && next_edge == graph.edges.size(), Error::kInvalidGraph);
  return flow;
}

bool Available(const ValueProgram& graph, const DominatorTree& dominators,
               ValueId value, WorkBlockId block, std::uint32_t order) {
  const auto& definition = graph.values[value.value];
  if (std::holds_alternative<EntryDefinition>(definition.origin)) return true;
  if (const auto* result = std::get_if<OpResult>(&definition.origin)) {
    const auto& op = graph.ops[result->op.value];
    if (op.exceptional == ExceptionalCompletion::kAlwaysThrow) return false;
    if (result->commit)
      return dominators.Dominates({result->commit->value}, {block.value});
    if (op.block == block) return op.order < order;
    return dominators.Dominates({op.block.value}, {block.value});
  }
  if (const auto* caught = std::get_if<CatchDefinition>(&definition.origin))
    return dominators.Dominates({caught->block.value}, {block.value});
  const auto phi = std::get<PhiDefinition>(definition.origin);
  return dominators.Dominates({phi.block.value}, {block.value});
}
}  // namespace

bool ValueAvailableAt(const ValueProgram& graph, const DominatorTree& dominators,
                      ValueId value, WorkBlockId block, std::uint32_t order) {
  return Available(graph, dominators, value, block, order);
}

static std::variant<ValueFlowProof, ValueAvailabilityError> VerifyAvailability(
    const ValueProgram& graph, SemanticBudget budget) {
  try {
    FlowQuota quota{budget};
    quota.Admit(graph.uses.size(), sizeof(std::uint8_t));
    quota.Work(graph.values.size());
    quota.Work(graph.ops.size());
    quota.Work(graph.uses.size());
    quota.Work(graph.uses.size());
    quota.Work(graph.uses.size());
    if (const auto error = VerifyValueUses(graph, graph.uses.size()))
      throw *error == ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kInvalidGraph;
    std::vector<std::size_t> predecessors;
    auto flow = ReadFlow(graph, quota, predecessors);
    const auto allowance = quota.Remaining();
    auto built = BuildNormalDominators(flow, {}, {allowance.bytes, allowance.work});
    auto* dominators = std::get_if<DominatorTree>(&built);
    if (!dominators) {
      const auto error = std::get<DominatorError>(built);
      if (error == DominatorError::kWorkLimit) return Error::kWorkLimit;
      if (error == DominatorError::kResourceLimit) return Error::kResourceLimit;
      return Error::kInvalidGraph;
    }
    quota.Work(dominators->work.steps);
    // Conservatively retain the builder's peak allocation charge, although its
    // temporary arrays have already been released. No independent full budget.
    quota.Admit(dominators->work.allocated_bytes, 1);
    for (std::size_t b = 0; b < dominators->nodes.size(); ++b) {
      quota.Work();
      if (dominators->nodes[b].begin != 0) continue;
      // An explicit throw has no normal Commit. Retain its provenance-only
      // phase, but never admit an arbitrary unreachable operation or value.
      Require(!graph.phases.empty(), Error::kUnreachableBlock);
      const auto& phase = graph.phases[b];
      Require(phase.phase == ExceptionPhase::kCommit && phase.operation,
          Error::kUnreachableBlock);
      const auto& op = graph.ops[phase.operation->value];
      Require(std::holds_alternative<ValueThrow>(op.operation) &&
          op.exceptional == ExceptionalCompletion::kAlwaysThrow &&
          dominators->nodes[op.block.value].begin != 0, Error::kUnreachableBlock);
    }
    std::vector<PhiKey> keys;
    std::vector<std::size_t> counts;
    quota.Reserve(keys, graph.uses.size());
    quota.Reserve(counts, graph.values.size());
    quota.Work(graph.values.size());
    counts.resize(graph.values.size());
    for (const auto& use : graph.uses) {
      quota.Work();
      if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (!value) continue;
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        const auto& op = graph.ops[owner->value];
        Require(Available(graph, *dominators, *value, op.block, op.order),
                Error::kUnavailableValue);
      } else {
        const auto phi_owner = std::get<ValueId>(use.owner);
        const auto phi = std::get<PhiDefinition>(graph.values[phi_owner.value].origin);
        ++counts[phi_owner.value];
        if (const auto* edge = std::get_if<EdgeId>(&*use.incoming)) {
          Require(edge->value < graph.edges.size(), Error::kInvalidPhi);
          const auto& incoming = graph.edges[edge->value];
          Require(incoming.target == phi.block, Error::kInvalidPhi);
          Require(Available(graph, *dominators, *value, incoming.source,
                            graph.blocks[incoming.source.value].op_count),
                  Error::kUnavailableValue);
          keys.push_back({phi_owner, *edge});
        } else {
          Require(phi.block.value == 0, Error::kInvalidPhi);
          Require(std::holds_alternative<EntryDefinition>(graph.values[value->value].origin),
                  Error::kUnavailableValue);
          keys.push_back({phi_owner, {}});
        }
      }
    }
    // Sorting sparse occurrence keys detects duplicate parallel-edge inputs
    // without a dense values-by-edges table or one CFG scan per phi.
    std::sort(keys.begin(), keys.end(), [&](const PhiKey& a, const PhiKey& b) {
      quota.Work();
      return a < b;
    });
    for (std::size_t i = 1; i < keys.size(); ++i) {
      quota.Work();
      Require(keys[i - 1] != keys[i], Error::kInvalidPhi);
    }
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Work();
      const auto& value = graph.values[i];
      const auto* phi = std::get_if<PhiDefinition>(&value.origin);
      if (!phi || value.life != ValueLife::kLive) continue;
      const auto expected = predecessors[phi->block.value] + (phi->block.value == 0);
      Require(counts[i] == expected && expected != 0, Error::kInvalidPhi);
    }
    return ValueFlowProof{graph.revision, graph.cfg_revision,
                          budget.work - quota.Remaining().work,
                          budget.bytes - quota.Remaining().bytes, std::move(*dominators)};
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}

std::variant<ValueFlowProof, ValueAvailabilityError> VerifyValueFlow(
    const ValueProgram& graph, SemanticBudget budget) {
  if (!graph.phases.empty()) return Error::kInvalidGraph;
  return VerifyAvailability(graph, budget);
}

std::variant<PhasedValueFlowProof, ValueAvailabilityError> VerifyPhasedValueFlow(
    const ValueProgram& graph, SemanticBudget budget) {
  if (graph.phases.empty()) return Error::kInvalidGraph;
  auto checked = VerifyAvailability(graph, budget);
  if (auto* proof = std::get_if<ValueFlowProof>(&checked))
    return PhasedValueFlowProof{proof->revision, proof->cfg_revision, proof->work, proof->allocated_bytes, std::move(proof->dominators)};
  return std::get<ValueAvailabilityError>(checked);
}
}  // namespace abcd::analysis
